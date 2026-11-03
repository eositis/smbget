/* SMBGET - dual-pane ProDOS <-> SMB file copier for the Apple II */
#include <conio.h>
#include <apple2enh.h>
#include <stdlib.h>
#include <string.h>
#include "copy.h"
#include "pane.h"
#include "prodos.h"
#include "ui.h"
#include "net.h"
#include "smb2.h"
#include "build.h"

#define VERSION "1.0"
#define MAX_VOLS 14

static pane_t panes[2];
static uint8_t active;                   /* 0 = left, 1 = right */
extern uint8_t eth_outp[];
#define vols ((vol_t *)eth_outp)     /* volume list: scratch in ip65's send buffer; only valid until
                                        the next directory read or SMB operation */
static uint8_t nvols;

#define ACT (&panes[active])
#define OTH (&panes[1 - active])

/* ---- screen ------------------------------------------------------------- */

static uint8_t server_ip[4];
static uint8_t log_row;

static void draw_title(void)
{
    uint8_t i;
    char ip[16];
    revers(1);
    gotoxy(0, ROW_TITLE);
    cputs(" SMBGET " VERSION " " BUILD_ID "  ProDOS <-> SMB");
    for (i = 33; i < SCR_W - 1; ++i) cputc(' ');
    if (net.up) {
        ip_str(net.ip, ip);
        cputsxy(50, ROW_TITLE, "Net: ");
        cputs(ip);
    } else {
        cputsxy(50, ROW_TITLE, "Net: not connected");
    }
    revers(0);
}

static void draw_keys(void)
{
    ui_text(1, ROW_KEYS,     "TAB Pane  RET Open  SPC Tag  A TagAll  C Copy  D Delete  R Rename  M MkDir");
    ui_text(1, ROW_KEYS + 1, "V Volumes  S Setup  N Net  ? Help  Q Quit");
}

static void draw_info(void)
{
    const pane_t *p = ACT;
    char n[12];

    ui_clear_row(ROW_INFO);
    gotoxy(1, ROW_INFO);
    fmt_num(n, p->tagged, 1);
    cputs(n);
    cputs(" tagged | ");
    if (p->kind == PK_LOCAL) {
        fmt_num(n, p->free_blocks, 5);
        cputs(n);
        cputs(" blks free");
        if (p->truncated) cputs(" | list truncated");
    } else {
        cputs("SMB");
    }
}

static void draw_all(void)
{
    clrscr();
    draw_title();
    pane_draw(&panes[0], active == 0);
    pane_draw(&panes[1], active == 1);
    draw_info();
    draw_keys();
}

static void refresh_panes(void)
{
    pane_draw(&panes[0], active == 0);
    pane_draw(&panes[1], active == 1);
    draw_info();
}

/* ---- loading ------------------------------------------------------------ */

static void load_pane(pane_t *p)
{
    uint8_t err;
    ui_msg("Reading directory...");
    err = pane_load(p);
    ui_clear_row(ROW_MSG);
    if (err >= 0x80) ui_msg2("SMB: ", smb_errstr((uint8_t)(err - 0x80)));
    else if (err) ui_msg2("Error: ", pd_errstr(err));
}

static void reload_both(void)
{
    uint8_t i;
    for (i = 0; i < 2; ++i) {
        uint16_t cur = panes[i].cur, top = panes[i].top;
        if (panes[i].kind == PK_SMB && !panes[i].loaded) continue;
        load_pane(&panes[i]);
        if (cur < panes[i].count) { panes[i].cur = cur; panes[i].top = top; }
    }
}

/* ---- volume picker ------------------------------------------------------ */

static uint8_t pick_volume(pane_t *p)
{
    uint8_t err;
    int8_t sel;

    err = pd_online(vols, MAX_VOLS, &nvols);
    if (err || nvols == 0) {
        ui_alert("Volumes", err ? pd_errstr(err) : (const char *)"No ProDOS volumes online", 0);
        return 0;
    }
    sel = ui_pick_volume(vols, nvols, 0);
    if (sel < 0) return 0;
    p->kind = PK_LOCAL;
    p->path[0] = '/';
    strcpy(p->path + 1, vols[sel].name);
    load_pane(p);
    return 1;
}

/* ---- navigation --------------------------------------------------------- */

static void go_up(pane_t *p)
{
    if (p->kind == PK_SMB) {
        if (p->path[1]) {
            pd_path_parent(p->path);
            if (!p->path[0]) strcpy(p->path, "/");
            load_pane(p);
        }
        return;
    }
    if (p->kind != PK_LOCAL) return;
    pd_path_parent(p->path);
    if (p->path[0] == 0) {                    /* above the volume root */
        if (!pick_volume(p)) {                /* cancelled: stay on the same volume */
            if (nvols) { p->path[0] = '/'; strcpy(p->path + 1, vols[0].name); }
        }
        return;
    }
    load_pane(p);
}

static void open_cur(pane_t *p)
{
    fentry_t *f = pane_cur(p);
    char hn[80];
    if (!f || (p->kind == PK_SMB && !p->loaded)) return;
    if (f->flags & F_UP) { go_up(p); return; }
    if (f->flags & F_DIR) {
        if (p->kind == PK_SMB && !p->path[1]) p->path[0] = 0;     /* root "/" + "/name" */
        pane_hostname(p, f, hn);
        if (!pd_path_join(p->path, hn)) {
            ui_msg("Path too long");
            return;
        }
        load_pane(p);
    } else {
        ui_msg("Not a directory (use C to copy)");
    }
}

/* ---- file operations ----------------------------------------------------- */

static uint8_t local_only(void)
{
    if (ACT->kind != PK_LOCAL || OTH->kind != PK_LOCAL) {
        ui_alert("SMB", "Not available for SMB yet", 0);
        return 0;
    }
    return 1;
}

/* path scratch for do_copy (kept off the small C stack) */
static char sp[PD_PATH_MAX + 1], dp[PD_PATH_MAX + 1], hn[80];

static void do_copy(void)
{
    pane_t *src = ACT, *dst = OTH;
    uint16_t i, copied = 0, skipped = 0, dirs = 0;
    finfo_t fi;
    uint8_t err, all = 0, none = 0, single = (uint8_t)(src->tagged == 0), smbdown, smbup;
    char c;

    smbdown = (uint8_t)(src->kind == PK_SMB && dst->kind == PK_LOCAL && src->loaded);
    smbup = (uint8_t)(src->kind == PK_LOCAL && dst->kind == PK_SMB && dst->loaded);
    if (!smbdown && !smbup && !local_only()) return;
    if (!smbdown && !smbup && strcmp(src->path, dst->path) == 0) {
        ui_alert("Copy", "Source and target are the same folder.", 0);
        return;
    }
    for (i = 0; i < src->count; ++i) {
        fentry_t fe, *f = &fe;
        pane_get(src, i, f);
        if (single) {
            if (i != src->cur) continue;
        } else if (!(f->flags & F_TAG)) {
            continue;
        }
        if (f->flags & F_UP) continue;
        if (f->flags & F_DIR) { ++dirs; continue; }

        strcpy(sp, src->path);
        if (smbdown) {
            pane_hostname(src, f, hn);
            if (!sp[1]) sp[0] = 0;
            pd_path_join(sp, hn);
        } else {
            pd_path_join(sp, f->name);
        }
        strcpy(dp, dst->path);
        if (smbup) {
            names_prodos_to_host(f->name, f->type, f->aux, hn);
            if (!dp[1]) dp[0] = 0;
            pd_path_join(dp, hn);
            err = copy_smb_up(sp, dp, f->name, 0);
            if (err == CP_EXISTS && !none) {
                c = all ? 'A' : ui_overwrite(f->name);
                if (c == 'C') break;
                if (c == 'S') { ++skipped; continue; }
                if (c == 'N') { none = 1; ++skipped; continue; }
                if (c == 'A') all = 1;
                err = copy_smb_up(sp, dp, f->name, 1);
            } else if (err == CP_EXISTS) {
                ++skipped; continue;
            }
            goto done_one;
        }
        pd_path_join(dp, f->name);

        if (!pd_getinfo(dp, &fi)) {               /* target exists */
            if (none) { ++skipped; continue; }
            if (!all) {
                c = ui_overwrite(f->name);
                if (c == 'C') break;
                if (c == 'S') { ++skipped; continue; }
                if (c == 'N') { none = 1; ++skipped; continue; }
                if (c == 'A') all = 1;
            }
            pd_destroy(dp);
        }
        err = smbdown ? copy_smb_down(sp, dp, f->type, f->aux, f->name) : copy_local_file(sp, dp, f->name);
done_one:
        if (err == CP_ABORT) { ui_msg("Copy aborted"); break; }
        if (err) {
            ui_alert("Copy failed", f->name, err == CP_SMB ? smb_errstr(SMB_E_STATUS) : err == CP_BAS ? (const char *)"BASIC conversion failed (SMBGET.OVL?)" : pd_errstr(err));
            break;
        }
        ++copied;
    }
    reload_both();
    draw_all();
    {
        char n[12];
        fmt_num(n, copied, 1);
        ui_msg2("Files copied: ", n);
        if (skipped || dirs) {
            cputs(skipped ? "  (skipped existing)" : "");
            if (dirs) cputs("  (folders not copied yet)");
        }
    }
}

static void do_delete(void)
{
    pane_t *p = ACT;
    uint16_t i, n = 0, last = 0;
    char path[PD_PATH_MAX + 1], num[12];
    uint8_t single = (uint8_t)(p->tagged == 0), err;
    fentry_t fe, *f;

    if (p->kind != PK_LOCAL) { local_only(); return; }
    for (i = 0; i < p->count; ++i) {
        f = &fe;
        pane_get(p, i, f);
        if (f->flags & F_UP) continue;
        if (single ? (i == p->cur) : (f->flags & F_TAG)) { ++n; last = i; }
    }
    if (n == 0) return;
    if (n == 1) {
        pane_get(p, last, &fe);
        if (!ui_confirm("Delete", fe.name, "Delete this item?")) { draw_all(); return; }
    } else {
        fmt_num(num, n, 1);
        strcat(num, " tagged files");
        if (!ui_confirm("Delete", num, "Delete all of them?")) { draw_all(); return; }
    }
    for (i = 0; i < p->count; ++i) {
        f = &fe;
        pane_get(p, i, f);
        if (f->flags & F_UP) continue;
        if (!(single ? (i == p->cur) : (f->flags & F_TAG))) continue;
        strcpy(path, p->path);
        pd_path_join(path, f->name);
        err = pd_destroy(path);
        if (err) { ui_alert("Delete failed", f->name, pd_errstr(err)); break; }
    }
    reload_both();
    draw_all();
}

static void do_rename(void)
{
    pane_t *p = ACT;
    fentry_t *f = pane_cur(p);
    char name[PD_NAME_MAX + 1], from[PD_PATH_MAX + 1], to[PD_PATH_MAX + 1];
    uint8_t err;

    if (p->kind != PK_LOCAL) { local_only(); return; }
    if (!f || (f->flags & F_UP)) return;
    strcpy(name, f->name);
    if (!ui_input("Rename", "New name:", name, PD_NAME_MAX)) { draw_all(); return; }
    strcpy(from, p->path); pd_path_join(from, f->name);
    strcpy(to, p->path);   pd_path_join(to, name);
    err = pd_rename(from, to);
    if (err) ui_alert("Rename failed", pd_errstr(err), 0);
    reload_both();
    draw_all();
}

static void do_mkdir(void)
{
    pane_t *p = ACT;
    char name[PD_NAME_MAX + 1], path[PD_PATH_MAX + 1];
    uint8_t err;

    if (p->kind != PK_LOCAL) { local_only(); return; }
    name[0] = 0;
    if (!ui_input("Make directory", "Name:", name, PD_NAME_MAX) || !name[0]) { draw_all(); return; }
    strcpy(path, p->path);
    pd_path_join(path, name);
    err = pd_mkdir(path);
    if (err) ui_alert("MkDir failed", pd_errstr(err), 0);
    reload_both();
    draw_all();
}

/* ---- network self-test ---------------------------------------------------- */

static char last_share[20];

static void net_log(const char *m)
{
    if (log_row > 17) {
        cclearxy(13, 7, 54);
        log_row = 7;
    }
    cclearxy(13, log_row, 54);
    cputsxy(13, log_row, m);
    ++log_row;
}

static void do_network(void)
{
    uint8_t err;
    char in[16], m[40];
    uint8_t sip[4];

    ui_box(10, 4, 60, 16, "Network test");
    log_row = 6;
    if (!net.up) {
        net_log("Scanning for Uthernet II...");
        err = net_start(net_log);
        if (err) {
            net_log(net_errstr(err));
            net_log("Press any key");
            cgetc();
            draw_all();
            return;
        }
    }
    draw_all();

    ip_str(server_ip, in);
    if (!server_ip[0]) in[0] = 0;
    if (ui_input("SMB server", "Server IP:", in, 15) && ip_parse(in, sip)) {
        char share[20];
        memcpy(server_ip, sip, 4);
        strcpy(share, last_share);
        if (ui_input("SMB share", "Share name (guest access):", share, 19) && share[0]) {
            strcpy(last_share, share);
            draw_all();
            ui_box(10, 4, 60, 16, "SMB connect");
            log_row = 6;
            err = smb_connect(server_ip, share, net_log);
            if (err) {
                char st[9];
                strcpy(m, smb_errstr(err));
                net_hex(st, smb_status);
                net_log(m);
                net_log("NT status:");
                cputs(st);
            } else {
                net_log("Connected to share (guest).");
                net_log("Reading directory...");
                strcpy(panes[1].path, "/");
                panes[1].kind = PK_SMB;
                if (pane_load(&panes[1])) { net_log("Directory read failed"); err = 1; }
            }
            if (err) {
                net_log("Press any key");
                cgetc();
            }
        }
    }
    draw_all();
}

static void show_help(void)
{
    ui_box(10, 3, 60, 10, "Help");
    ui_text(13, 5,  "TAB pane  RET open  LEFT up  SPC tag  A all");
    ui_text(13, 6,  "V vol  C copy  D del  R ren  M mkdir  N SMB  Q quit");
    cgetc();
    draw_all();
}

/* ---- main --------------------------------------------------------------- */

static void init_panes(void)
{
    char pfx[PD_PATH_MAX + 1];
    uint8_t i;

    panes[0].auxbase = 0;
    panes[1].auxbase = (uint16_t)(MAX_ENTRIES * sizeof(fentry_t));
    panes[0].heapbase = (uint16_t)(2u * MAX_ENTRIES * sizeof(fentry_t));
    panes[1].heapbase = (uint16_t)(panes[0].heapbase + NAMEHEAP);
    panes[0].x = 0;
    panes[1].x = 40;
    panes[1].kind = PK_SMB;

    if (pd_getprefix(pfx)) {
        uint8_t err = pd_online(vols, MAX_VOLS, &nvols);
        pfx[0] = 0;
        if (!err && nvols) { pfx[0] = '/'; strcpy(pfx + 1, vols[0].name); }
    }
    strcpy(panes[0].path, pfx);
    copy_set_home(pfx);
    for (i = 0; i < 2; ++i) {
        panes[i].top = panes[i].cur = panes[i].count = panes[i].tagged = 0;
    }
    if (panes[0].path[0]) pane_load(&panes[0]);
}

void main(void)
{
    char c;
    pane_t *p;

    if (!aux_present()) {
        cputs("SMBGET needs a 128K Apple IIe/IIc (aux memory).\r\n");
        cgetc();
        return;
    }
    ui_init();
    init_panes();
    active = 0;
    draw_all();
    if (!panes[0].path[0]) pick_volume(&panes[0]), draw_all();

    for (;;) {
        p = ACT;
        c = cgetc();
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        switch (c) {
        case '\t':
            active = (uint8_t)(1 - active);
            pane_draw_row(&panes[1 - active], panes[1 - active].cur, 0);
            pane_draw_row(&panes[active], panes[active].cur, 1);
            pane_draw(&panes[0], active == 0);
            pane_draw(&panes[1], active == 1);
            draw_info();
            break;
        case CH_CURS_UP:   pane_move(p, -1, 1); break;
        case CH_CURS_DOWN: pane_move(p, 1, 1); break;
        case '<':          pane_move(p, -LIST_ROWS, 1); break;
        case '>':          pane_move(p, LIST_ROWS, 1); break;
        case CH_ENTER:     open_cur(p); refresh_panes(); break;
        case CH_CURS_LEFT:
        case CH_ESC:       go_up(p); draw_all(); break;
        case ' ':          pane_tag(p); pane_move(p, 1, 1); draw_info(); break;
        case 'A':          pane_tag_all(p); pane_draw(p, 1); draw_info(); break;
        case 'V':          pick_volume(p); draw_all(); break;
        case 'C':          do_copy(); break;
        case 'D':          do_delete(); break;
        case 'R':          do_rename(); break;
        case 'M':          do_mkdir(); break;
        case 'N':          do_network(); break;
        case 'S':
            ui_alert("Setup", "Setup screen is not built yet.", "Use N for the network test.");
            draw_all();
            break;
        case '?':          show_help(); break;
        case 'Q':
            if (ui_confirm("Quit", "Quit SMBGET?", 0)) { ui_done(); exit(0); }
            draw_all();
            break;
        }
    }
}
