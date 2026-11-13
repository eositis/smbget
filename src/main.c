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
#include "tree.h"
#include "build.h"

void __fastcall__ lc_move(uint16_t dest, const uint8_t *src, uint16_t n);
uint8_t lc_ok(void);

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
char last_share[20];
static uint8_t log_row;

static void draw_title(void)
{
    uint8_t i;
    char ip[16];
    revers(1);
    gotoxy(0, ROW_TITLE);
    cputs(" SMBGET " VERSION " " BUILD_ID "  ProDOS <-> SMB");
    for (i = 33; i < SCR_W - 1; ++i) cputc(' ');
    if (smb_connected()) {
        ip_str(server_ip, ip);
        cputsxy(54, ROW_TITLE, "SMB host: ");
        cputs(ip);
    } else {
        cputsxy(54, ROW_TITLE, "SMB host: not connected");
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
        ui_alert("SMB", "Not connected", 0);
        return 0;
    }
    return 1;
}

/* path scratch (kept off the small C stack; the language-card walk uses it too) */
char sp[PD_PATH_MAX + 1], dp[PD_PATH_MAX + 1], hn[80];
uint8_t smb_src, smb_dst;
static uint8_t cp_all, cp_none;
uint16_t cp_files, cp_skip;
char cp_who[PD_NAME_MAX + 1];
uint16_t tree_ix[TREE_MAX];
uint8_t tree_sl[TREE_MAX], tree_dl[TREE_MAX];
uint16_t pick_n, pick_i;
fentry_t pick_ent;
uint8_t pick_hit, pick_err, pick_smb;

static uint8_t smb_ready(const pane_t *p)
{
    if (p->kind != PK_SMB) return 1;
    if (p->loaded && smb_connected()) return 1;
    ui_alert("SMB", "Not connected", 0);
    return 0;
}

/* Share root is stored as "/"; the joiner would otherwise make "//name". */
uint8_t join_share(char *path, const char *name)
{
    if (path[0] == '/' && path[1] == 0) path[0] = 0;
    return pd_path_join(path, name);
}

static uint8_t paths_for(const pane_t *src, const pane_t *dst, const fentry_t *f)
{
    strcpy(sp, src->path);
    if (smb_src) {
        pane_hostname(src, f, hn);
        if (!join_share(sp, hn)) return 0;
    } else if (!pd_path_join(sp, f->name)) {
        return 0;
    }
    strcpy(dp, dst->path);
    if (smb_dst) {
        names_prodos_to_host(f->name, f->type, f->aux, hn);
        if (!join_share(dp, hn)) return 0;
    } else if (!pd_path_join(dp, f->name)) {
        return 0;
    }
    return 1;
}

static uint8_t pick_cb(const fentry_t *f)
{
    uint8_t i;

    if (f->flags & F_UP) return 0;
    if (pick_i != pick_n) { ++pick_i; return 0; }
    memcpy(&pick_ent, f, sizeof pick_ent);
    hn[0] = 0;
    if (pick_smb && dir_hname) {
        for (i = 0; dir_hname[i] && i < 79; ++i) hn[i] = dir_hname[i];
        hn[i] = 0;
    }
    pick_hit = 1;
    return 1;
}

/* The n-th child of the directory in sp. 0 = no more. Does not recurse. */
uint8_t next_child(void)
{
    pick_i = 0;
    pick_hit = 0;
    if (pick_smb) pick_err = smb_readdir(sp, pick_cb);
    else pick_err = pd_readdir(sp, pick_cb);
    if (pick_err) return 0;
    return pick_hit;
}

uint8_t ensure_dir(void)
{
    uint8_t err;

    if (smb_dst) {
        err = smb_mkdir(dp);
        if (!err) return 0;
        if (smb_status == 0xC0000035UL) return 0;
        return CP_SMB;
    }
    err = pd_mkdir(dp);
    if (err == PD_ERR_DUP) return 0;
    return err;
}

uint8_t copy_file_paths(const char *label, uint8_t type, uint16_t aux, uint16_t date)
{
    uint8_t err;
    finfo_t fi;
    char c;

    if (smb_dst) {
        err = copy_smb_up(sp, dp, label, 0);
        if (err != CP_EXISTS) return err;
        if (cp_none) return CP_EXISTS;
        if (cp_all) c = 'A';
        else c = ui_overwrite(label);
        if (c == 'C') return CP_ABORT;
        if (c == 'S' || c == 'N') {
            if (c == 'N') cp_none = 1;
            return CP_EXISTS;
        }
        if (c == 'A') cp_all = 1;
        return copy_smb_up(sp, dp, label, 1);
    }
    if (!pd_getinfo(dp, &fi)) {
        if (cp_none) return CP_EXISTS;
        if (!cp_all) {
            c = ui_overwrite(label);
            if (c == 'C') return CP_ABORT;
            if (c == 'S' || c == 'N') {
                if (c == 'N') cp_none = 1;
                return CP_EXISTS;
            }
            if (c == 'A') cp_all = 1;
        }
        pd_destroy(dp);
    }
    if (smb_src) return copy_smb_down(sp, dp, type, aux, label, date);
    return copy_local_file(sp, dp, label);
}

static void copy_failed(uint8_t err, const char *who)
{
    const char *why;

    if (err == CP_ABORT) { ui_msg("Copy aborted"); return; }
    if (err >= CP_LONG) { ui_msg("Path too long"); return; }
    if (err == CP_SMB) why = smb_errstr(SMB_E_STATUS);
    else if (err == CP_BAS) why = "Need SMBGET.OVL";
    else why = pd_errstr(err);
    ui_alert("Copy failed", who, why);
}

static void do_copy(void)
{
    pane_t *src = ACT, *dst = OTH;
    uint16_t i;
    uint8_t err, single, smbdown, smbup;
    char n[12];

    smbdown = (uint8_t)(src->kind == PK_SMB && dst->kind == PK_LOCAL && src->loaded);
    smbup = (uint8_t)(src->kind == PK_LOCAL && dst->kind == PK_SMB && dst->loaded);
    smb_src = smbdown;
    smb_dst = smbup;
    if (!smbdown && !smbup && !local_only()) return;
    if (!smbdown && !smbup && strcmp(src->path, dst->path) == 0) {
        ui_alert("Copy", "Source and target are the same folder.", 0);
        return;
    }
    cp_all = cp_none = 0;
    cp_files = cp_skip = 0;
    single = (uint8_t)(src->tagged == 0);
    for (i = 0; i < src->count; ++i) {
        pane_get(src, i, &pick_ent);
        if (pick_ent.flags & F_UP) continue;
        if (single) {
            if (i != src->cur) continue;
        } else if (!(pick_ent.flags & F_TAG)) {
            continue;
        }
        memcpy(cp_who, pick_ent.name, PD_NAME_MAX);
        cp_who[PD_NAME_MAX] = 0;
        if (!paths_for(src, dst, &pick_ent)) { copy_failed(CP_LONG, cp_who); break; }
        if (pick_ent.flags & F_DIR) err = copy_tree();
        else {
            err = copy_file_paths(pick_ent.name, pick_ent.type, pick_ent.aux, pick_ent.date);
            if (!err) ++cp_files;
        }
        if (err == CP_EXISTS) { ++cp_skip; continue; }
        if (err) { copy_failed(err, cp_who); break; }
    }
    reload_both();
    draw_all();
    fmt_num(n, cp_files, 1);
    ui_msg2("Files copied: ", n);
    if (cp_skip) cputs("  (skipped existing)");
}

static uint8_t item_path(const pane_t *p, const fentry_t *f)
{
    if (p->kind == PK_SMB) {
        pane_hostname(p, f, hn);
        strcpy(sp, p->path);
        return join_share(sp, hn);
    }
    strcpy(sp, p->path);
    return pd_path_join(sp, f->name);
}

static void do_delete(void)
{
    pane_t *p = ACT;
    uint16_t i;
    uint8_t single, err, share;

    if (!smb_ready(p)) return;
    share = (uint8_t)(p->kind == PK_SMB);
    single = (uint8_t)(p->tagged == 0);
    if (single) {
        pane_get(p, p->cur, &pick_ent);
        if (pick_ent.flags & F_UP) return;
        if (!ui_confirm("Delete", pick_ent.name, "Delete this item?")) { draw_all(); return; }
    } else if (!ui_confirm("Delete", "Delete every tagged item?", 0)) {
        draw_all();
        return;
    }
    for (i = 0; i < p->count; ++i) {
        pane_get(p, i, &pick_ent);
        if (pick_ent.flags & F_UP) continue;
        if (single) {
            if (i != p->cur) continue;
        } else if (!(pick_ent.flags & F_TAG)) {
            continue;
        }
        if (!item_path(p, &pick_ent)) { ui_msg("Path too long"); break; }
        if (share) err = smb_remove(sp, (uint8_t)((pick_ent.flags & F_DIR) != 0));
        else err = pd_destroy(sp);
        if (!err) continue;
        if (share) ui_alert("Delete failed", pick_ent.name, smb_errstr(err));
        else ui_alert("Delete failed", pick_ent.name, pd_errstr(err));
        break;
    }
    reload_both();
    draw_all();
}

static void do_rename(void)
{
    pane_t *p = ACT;
    fentry_t *f = pane_cur(p);
    uint8_t err, share;

    if (!f || (f->flags & F_UP) || !smb_ready(p)) return;
    share = (uint8_t)(p->kind == PK_SMB);
    if (share) {
        if (!item_path(p, f)) { ui_msg("Path too long"); draw_all(); return; }
        if (!ui_input("Rename", "New name:", hn, 40) || !hn[0]) { draw_all(); return; }
        strcpy(dp, p->path);
        if (!join_share(dp, hn)) { ui_msg("Path too long"); draw_all(); return; }
        err = smb_rename(sp, dp);
        if (err) ui_alert("Rename failed", smb_errstr(err), 0);
    } else {
        strcpy(hn, f->name);
        if (!ui_input("Rename", "New name:", hn, PD_NAME_MAX) || !hn[0]) { draw_all(); return; }
        strcpy(sp, p->path);
        pd_path_join(sp, f->name);
        strcpy(dp, p->path);
        pd_path_join(dp, hn);
        err = pd_rename(sp, dp);
        if (err) ui_alert("Rename failed", pd_errstr(err), 0);
    }
    reload_both();
    draw_all();
}

static void do_mkdir(void)
{
    pane_t *p = ACT;
    uint8_t err, share;

    if (!smb_ready(p)) return;
    share = (uint8_t)(p->kind == PK_SMB);
    hn[0] = 0;
    {
        uint8_t maxlen = PD_NAME_MAX;
        if (share) maxlen = 40;
        if (!ui_input("Make directory", "Name:", hn, maxlen) || !hn[0]) {
            draw_all();
            return;
        }
    }
    strcpy(sp, p->path);
    if (share) {
        if (!join_share(sp, hn)) { ui_msg("Path too long"); draw_all(); return; }
        err = smb_mkdir(sp);
        if (err) ui_alert("MkDir failed", smb_errstr(err), 0);
    } else {
        if (!pd_path_join(sp, hn)) { ui_msg("Path too long"); draw_all(); return; }
        err = pd_mkdir(sp);
        if (err) ui_alert("MkDir failed", pd_errstr(err), 0);
    }
    reload_both();
    draw_all();
}

/* ---- network self-test ---------------------------------------------------- */

static void nlog(const char *m)
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
        nlog("Looking for a network card...");
        err = net_start(nlog);
        if (err) {
            nlog(net_errstr(err));
            nlog("Press any key");
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
            err = smb_connect(server_ip, share, nlog);
            if (err) {
                char st[9];
                strcpy(m, smb_errstr(err));
                net_hex(st, smb_status);
                nlog(m);
                nlog("NT status:");
                cputs(st);
            } else {
                nlog("Connected to share (guest).");
                nlog("Reading directory...");
                strcpy(panes[1].path, "/");
                panes[1].kind = PK_SMB;
                if (pane_load(&panes[1])) { nlog("Directory read failed"); err = 1; }
            }
            if (err) {
                nlog("Press any key");
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

/* SMBGET.NAM holds the name tables; they run from language-card bank 2. */
static uint8_t load_names(void)
{
    extern uint8_t smbpkt[];
    uint8_t err, ref = 0;
    uint16_t got, dest = 0xD400;

    if (!copy_home()[0]) return 1;
    strcpy(sp, copy_home());
    if (!pd_path_join(sp, "SMBGET.NAM")) return 1;
    err = pd_open(sp, 0, &ref);
    if (err) return err;
    for (;;) {
        err = pd_read(ref, smbpkt, 128, &got);
        if (got) {
            if (got > (uint16_t)(0xE000 - dest)) { err = 1; break; }
            lc_move(dest, smbpkt, got);
            dest = (uint16_t)(dest + got);
        }
        if (err == PD_ERR_EOF) { err = 0; break; }
        if (err || !got) break;
    }
    pd_close(ref);
    if (err) return err;
    if (!lc_ok()) return 1;
    return 0;
}

void main(void)
{
    char c;
    pane_t *p;
    uint8_t err;

    pd_unhook_ram();
    if (!aux_present()) {
        cputs("SMBGET needs a 128K Apple IIe/IIc (aux memory).\r\n");
        cgetc();
        return;
    }
    ui_init();
    init_panes();
    err = load_names();
    if (err) {
        if (err == 1) ui_alert("SMBGET.NAM", "Put it next to SMBGET.SYSTEM.", 0);
        else ui_alert("SMBGET.NAM", pd_errstr(err), 0);
        return;
    }
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
