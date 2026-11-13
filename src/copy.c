#include <conio.h>
#include <string.h>
#include "copy.h"
#include "prodos.h"
#include "ui.h"
#include "smb2.h"

static uint8_t *copybuf;                 /* borrows the SMB packet buffer */
static uint16_t COPYBUF;

static void progress(uint8_t x, uint8_t y, uint32_t done, uint32_t total)
{
    uint8_t i, fill = (uint8_t)(total ? (done * 40UL) / total : 40);
    gotoxy(x, y);
    cputc('[');
    for (i = 0; i < 40; ++i) cputc(i < fill ? '#' : '.');
    cputc(']');
}

uint8_t copy_local_file(const char *src, const char *dst, const char *label)
{
    finfo_t fi;
    uint8_t err, sref = 0, dref = 0, sopen = 0, dopen = 0, created = 0;
    uint16_t got;
    uint32_t done = 0, total;
    char tmp[12];

    copybuf = smb_scratch(&COPYBUF);
    err = pd_getinfo(src, &fi);
    if (err) return err;
    total = (uint32_t)fi.blocks * 512UL;

    ui_box(14, 7, 52, 10, "Copying");
    cputsxy(17, 9, label);
    cputsxy(17, 14, "ESC = abort");
    progress(17, 11, 0, total);

    err = pd_create(dst, fi.type, fi.aux);
    if (err) goto fail;
    created = 1;
    err = pd_open(src, 0, &sref);
    if (err) goto fail;
    sopen = 1;
    err = pd_open(dst, 1, &dref);
    if (err) goto fail;
    dopen = 1;

    for (;;) {
        if (kbhit() && cgetc() == CH_ESC) { err = CP_ABORT; goto fail; }
        err = pd_read(sref, copybuf, COPYBUF, &got);
        if (err == PD_ERR_EOF) { err = 0; break; }
        if (err) goto fail;
        if (got == 0) break;
        err = pd_write(dref, copybuf, got);
        if (err) goto fail;
        done += got;
        progress(17, 11, done, total);
        fmt_num(tmp, done >> 9, 6);
        cputsxy(17, 12, tmp);
        cputs(" blks");
        if (got < COPYBUF) break;
    }
    pd_close(dref);
    pd_close(sref);
    /* creation sets type/aux; make sure they survive the writes */
    pd_setinfo(dst, &fi);
    return 0;

fail:
    if (dopen) pd_close(dref);
    if (sopen) pd_close(sref);
    if (created) pd_destroy(dst);
    return err;
}


/* ---- BASIC <-> text: done by the overlay SMBGET.OVL (see src/ovl) -------------------------- */

#include "ovl/ovl.h"

extern uint8_t eth_inp[], eth_outp[];
static char home[PD_PATH_MAX + 1];           /* directory SMBGET.SYSTEM was started from */
static ovl_api_t api;

void copy_set_home(const char *dir)
{
    strcpy(home, dir);
}

const char *copy_home(void)
{
    return home;
}

static uint8_t ovl_aborted(void)
{
    return (uint8_t)(kbhit() && cgetc() == CH_ESC);
}

static void ovl_progress(uint32_t done, uint32_t total)
{
    char tmp[12];
    progress(17, 11, done, total);
    fmt_num(tmp, (uint16_t)(done >> 9), 6);
    cputsxy(17, 12, tmp);
    cputs(" blks");
}

static uint8_t OVL(uint8_t op, uint8_t ref, const uint8_t *p, uint16_t n, uint32_t total)
{
    return ((ovl_entry_fn)OVL_ADDR)(op, &api, ref, p, n, total);
}

/* Load SMBGET.OVL and check its signature. Must run while no ProDOS file is open in slot 0. */
static uint8_t ovl_load(void)
{
    char path[PD_PATH_MAX + 1];
    uint8_t err, ref;
    uint16_t got = 0;
    const uint8_t *o = (const uint8_t *)OVL_ADDR;

    strcpy(path, home);
    pd_path_join(path, "SMBGET.OVL");
    err = pd_open(path, 0, &ref);
    if (err) return CP_BAS;
    err = pd_read(ref, (void *)OVL_ADDR, OVL_MAXLEN, &got);
    pd_close(ref);
    if ((err && err != PD_ERR_EOF) || got < 10) return CP_BAS;
    if (o[0] != 0x4C || o[3] != 'S' || o[4] != 'M' || o[5] != 'B' || o[6] != 'O' || o[7] != 'V' || o[8] != 'L' || o[9] != '1')
        return CP_BAS;
    api.pd_read = pd_read;
    api.pd_write = pd_write;
    api.smb_write = smb_write;
    api.progress = ovl_progress;
    api.aborted = ovl_aborted;
    api.wbuf = smb_write_buf();
    api.pkt = smb_scratch(&got);
    api.inp = eth_inp;
    api.outp = eth_outp;
    return 0;
}

uint8_t copy_smb_down(const char *spath, const char *dst, uint8_t type, uint16_t aux,
                      const char *label, uint16_t date)
{
    uint8_t err, dref = 0, dopen = 0, created = 0, sopen = 0, first = 1, c;
    uint16_t got, n, i;
    uint32_t done = 0, total = 0;
    const uint8_t *d;
    uint8_t *w;
    char tmp[12];

    ui_box(14, 7, 52, 10, "Downloading");
    cputsxy(17, 9, label);
    cputsxy(17, 14, "ESC = abort");
    if (smb_open_file(spath, &total)) { err = CP_SMB; goto fail; }
    sopen = 1;
    progress(17, 11, 0, total);

    for (;;) {
        if (kbhit() && cgetc() == CH_ESC) { err = CP_ABORT; goto fail; }
        d = smb_read(done, type == PT_BAS ? 400 : SMB_READ_MAX, &got);
        if (!d) { err = CP_SMB; goto fail; }
        if (first) {
            first = 0;
            if (type == PT_BIN && aux == 0x0800) {        /* unknown type: text if it looks like text */
                for (i = 0; i < got; ++i) {
                    c = d[i];
                    if ((c < 32 && c != 9 && c != 10 && c != 13) || c > 126) break;
                }
                if (i == got && got) { type = PT_TXT; aux = 0; }
            }
            if (type == PT_BAS) {
                err = ovl_load();
                if (!err) err = OVL(OVL_DOWN_BEGIN, 0, 0, 0, 0);
                if (err) goto fail;
            }
            err = pd_create(dst, type, aux);
            if (err) goto fail;
            created = 1;
            err = pd_open(dst, 0, &dref);          /* slot 0: slot 1 shares memory with the network */
            if (err) goto fail;
            dopen = 1;
        }
        if (got == 0) break;
        w = (uint8_t *)d;
        n = got;
        if (type == PT_BAS) {                      /* text listing -> tokenized program */
            err = OVL(OVL_DOWN_FEED, dref, w, got, 0);
            if (err) goto fail;
            n = 0;
        } else if (type == PT_TXT && aux == 0) {          /* LF (or CRLF) -> CR */
            for (i = 0, n = 0; i < got; ++i) {
                c = w[i];
                if (c == '\r') continue;
                if (c == '\n') c = '\r';              /* (no ?: here: cc65 2.19 miscompiles it) */
                w[n++] = c;
            }
        }
        if (n && (err = pd_write(dref, w, n)) != 0) goto fail;
        done += got;
        progress(17, 11, done, total);
        fmt_num(tmp, (uint16_t)(done >> 9), 6);
        cputsxy(17, 12, tmp);
        cputs(" blks");
    }
    if (type == PT_BAS && (err = OVL(OVL_DOWN_END, dref, 0, 0, 0)) != 0) goto fail;
    pd_close(dref);
    smb_close_file();
    {
        finfo_t fi;
        fi.access = 0xC3;
        fi.type = type;
        fi.aux = aux;
        fi.date = date;
        pd_setinfo(dst, &fi);          /* keep the date the share reported */
    }
    return 0;

fail:
    if (dopen) pd_close(dref);
    if (sopen) smb_close_file();
    if (created) pd_destroy(dst);
    return err;
}

uint8_t copy_smb_up(const char *src, const char *spath, const char *label, uint8_t overwrite)
{
    uint8_t err, sref = 0, sopen = 0;
    uint16_t got, n;
    uint32_t done = 0, total;
    finfo_t fi;
    char tmp[12];

    err = pd_getinfo(src, &fi);
    if (err) return err;
    total = (uint32_t)fi.blocks * 512UL;
    err = smb_create_file(spath, overwrite);
    if (err) return (uint8_t)(smb_status == 0xC0000035UL ? CP_EXISTS : CP_SMB);

    ui_box(14, 7, 52, 10, "Uploading");
    cputsxy(17, 9, label);
    cputsxy(17, 14, "ESC = abort");
    progress(17, 11, 0, total);
    if (fi.type == PT_BAS && (err = ovl_load()) != 0) goto fail;
    err = pd_open(src, 0, &sref);
    if (err) goto fail;
    sopen = 1;
    if (fi.type == PT_BAS) {                       /* tokenized program -> text listing */
        err = OVL(OVL_UP, sref, 0, 0, total);
        if (err) goto fail;
        goto finish;
    }
    for (;;) {
        if (kbhit() && cgetc() == CH_ESC) { err = CP_ABORT; goto fail; }
        err = pd_read(sref, smb_write_buf(), SMB_WRITE_MAX, &got);
        if (err == PD_ERR_EOF) { err = 0; break; }
        if (err) goto fail;
        if (got == 0) break;
        if (fi.type == PT_TXT && fi.aux == 0) {    /* CR -> LF, drop the high bit */
            uint8_t *q = smb_write_buf();
            for (n = 0; n < got; ++n) {
                q[n] &= 0x7F;
                if (q[n] == '\r') q[n] = '\n';
            }
        }
        if (smb_write(done, got)) { err = CP_SMB; goto fail; }
        done += got;
        progress(17, 11, done, total);
        fmt_num(tmp, (uint16_t)(done >> 9), 6);
        cputsxy(17, 12, tmp);
        cputs(" blks");
    }
finish:
    pd_close(sref);
    smb_set_normal();
    smb_close_file();
    return 0;

fail:
    if (sopen) pd_close(sref);
    smb_close_file();                       /* the partial file stays on the share */
    return err;
}
