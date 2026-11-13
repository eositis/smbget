#include <string.h>
#include "prodos.h"

extern unsigned char __fastcall__ pd_mli(unsigned char cmd, void *params);

#define MLI_CREATE   0xC0
#define MLI_DESTROY  0xC1
#define MLI_RENAME   0xC2
#define MLI_SETINFO  0xC3
#define MLI_GETINFO  0xC4
#define MLI_ONLINE   0xC5
#define MLI_GETPFX   0xC7
#define MLI_OPEN     0xC8
#define MLI_READ     0xCA
#define MLI_WRITE    0xCB
#define MLI_CLOSE    0xCC

/* ---- Pascal strings --------------------------------------------------- */

static char pstr1[PD_PATH_MAX + 2];
static char pstr2[PD_PATH_MAX + 2];

static char *topstr(char *dst, const char *s)
{
    uint8_t n = (uint8_t)strlen(s);
    if (n > PD_PATH_MAX) n = PD_PATH_MAX;
    dst[0] = (char)n;
    memcpy(dst + 1, s, n);
    return dst;
}

extern uint8_t eth_inp[];

/* ---- I/O buffers: two page-aligned 1K buffers ------------------------
   Slot 0 has its own memory. Slot 1 borrows the Ethernet buffers (eth_inp and eth_outp
   are contiguous). The link checks that a page-aligned 1K fits in them. Slot 1 may
   only be used while the network is idle: local-to-local copy and directory listing,
   never an SMB transfer. */

#pragma bss-name (push, "IOBUF")
static uint8_t iobuf0[1024];        /* linked page-aligned (cfg/smbget.cfg) */
#pragma bss-name (pop)

static uint8_t *iobuf(uint8_t slot)
{
    return slot ? (uint8_t *)((((unsigned)eth_inp) + 255u) & 0xFF00u) : iobuf0;
}

/* ---- MLI parameter blocks ---------------------------------------------- */

static struct { uint8_t count; char *path; uint8_t access, type; uint16_t aux;
                uint8_t storage; uint16_t cdate, ctime; } p_create;
static struct { uint8_t count; char *path; } p_path1;
static struct { uint8_t count; char *path; char *newpath; } p_rename;
static struct { uint8_t count; char *path; uint8_t access, type; uint16_t aux;
                uint8_t storage; uint16_t blocks, mdate, mtime, cdate, ctime; } p_info;
static struct { uint8_t count; uint8_t unit; void *buf; } p_online;
static struct { uint8_t count; void *buf; } p_pfx;
static struct { uint8_t count; char *path; void *iobuf; uint8_t ref; } p_open;
static struct { uint8_t count; uint8_t ref; void *buf; uint16_t req, got; } p_rw;
static struct { uint8_t count; uint8_t ref; } p_close;

/* ON_LINE scratch: ip65's receive buffer, free between SMB operations */
#define onlinebuf eth_inp
static char pfxbuf[PD_PATH_MAX + 2];

/* ---- volumes ------------------------------------------------------------ */

uint8_t pd_online(vol_t *v, uint8_t max, uint8_t *count)
{
    uint8_t i, n, len, e, err;
    char path[PD_NAME_MAX + 2];
    finfo_t fi;

    p_online.count = 2;
    p_online.unit = 0;
    p_online.buf = onlinebuf;
    err = pd_mli(MLI_ONLINE, &p_online);
    *count = 0;
    if (err) return err;

    n = 0;
    for (i = 0; i < 16 && n < max; ++i) {
        uint8_t *rec = onlinebuf + (unsigned)i * 16;
        len = rec[0] & 0x0F;
        if (rec[0] == 0) break;
        if (len == 0) continue;               /* error entry */
        v[n].unit = rec[0] & 0xF0;
        memcpy(v[n].name, rec + 1, len);
        v[n].name[len] = 0;
        v[n].total = v[n].used = 0;
        path[0] = '/';
        strcpy(path + 1, v[n].name);
        e = pd_getinfo(path, &fi);
        if (!e) {
            v[n].total = fi.aux;
            v[n].used = fi.blocks;
        }
        ++n;
    }
    *count = n;
    return 0;
}

uint8_t pd_getprefix(char *out)
{
    uint8_t err, n;
    p_pfx.count = 1;
    p_pfx.buf = pfxbuf;
    err = pd_mli(MLI_GETPFX, &p_pfx);
    if (err) return err;
    n = (uint8_t)pfxbuf[0];
    if (n == 0) return PD_ERR_NOTFOUND;
    if (n > PD_PATH_MAX) n = PD_PATH_MAX;
    memcpy(out, pfxbuf + 1, n);
    if (out[n - 1] == '/') --n;
    out[n] = 0;
    return 0;
}

/* ---- directory listing -------------------------------------------------- */

uint8_t pd_readdir(const char *path, pd_dir_cb cb)
{
    uint8_t err, ref, first = 1, i, st, len;
    uint16_t got;
    uint8_t *e, *blockbuf = iobuf(1);      /* slot 1 is idle while listing */
    fentry_t fe, *f = &fe;

    err = pd_open(path, 0, &ref);
    if (err) return err;

    for (;;) {
        err = pd_read(ref, blockbuf, 512, &got);
        if (err == PD_ERR_EOF) { err = 0; break; }
        if (err) break;
        if (got < 512) break;
        for (i = (uint8_t)(first ? 1 : 0); i < 13; ++i) {
            e = blockbuf + 4 + (unsigned)i * 39;
            st = e[0] >> 4;
            len = e[0] & 0x0F;
            if (st == 0 || st >= 0xE || len == 0) continue;
            memcpy(f->name, e + 1, len);
            f->name[len] = 0;
            f->type = e[16];
            f->flags = (uint8_t)((st == 0x0D || e[16] == PT_DIR) ? F_DIR : 0);
            f->blocks = (uint16_t)(e[19] | (e[20] << 8));
            f->size = (uint32_t)e[21] | ((uint32_t)e[22] << 8) | ((uint32_t)e[23] << 16);
            f->aux = (uint16_t)(e[31] | (e[32] << 8));
            f->date = (uint16_t)(e[33] | (e[34] << 8));
            if (cb(f)) { err = 0; goto done; }
        }
        first = 0;
    }
done:
    pd_close(ref);
    return err;
}

/* ---- file info / create / rename / delete ------------------------------ */

uint8_t pd_getinfo(const char *path, finfo_t *fi)
{
    uint8_t err;
    p_info.count = 10;
    p_info.path = topstr(pstr1, path);
    err = pd_mli(MLI_GETINFO, &p_info);
    if (err) return err;
    fi->access = p_info.access;
    fi->type = p_info.type;
    fi->aux = p_info.aux;
    fi->storage = p_info.storage;
    fi->blocks = p_info.blocks;
    fi->date = p_info.mdate;
    fi->time = p_info.mtime;
    return 0;
}

uint8_t pd_setinfo(const char *path, const finfo_t *fi)
{
    memset(&p_info, 0, sizeof p_info);
    p_info.count = 7;
    p_info.path = topstr(pstr1, path);
    p_info.access = fi->access;
    p_info.type = fi->type;
    p_info.aux = fi->aux;
    p_info.blocks = 0;               /* null fields (storage type, blocks) */
    p_info.mdate = fi->date;         /* 0 = let ProDOS stamp today */
    p_info.mtime = 0;
    return pd_mli(MLI_SETINFO, &p_info);
}

static uint8_t create(const char *path, uint8_t type, uint16_t aux, uint8_t storage)
{
    p_create.count = 7;
    p_create.path = topstr(pstr1, path);
    p_create.access = 0xC3;          /* destroy, rename, write, read */
    p_create.type = type;
    p_create.aux = aux;
    p_create.storage = storage;
    p_create.cdate = 0;
    p_create.ctime = 0;
    return pd_mli(MLI_CREATE, &p_create);
}

uint8_t pd_create(const char *path, uint8_t type, uint16_t aux)
{
    return create(path, type, aux, 1);
}

uint8_t pd_mkdir(const char *path)
{
    return create(path, PT_DIR, 0, 0x0D);
}

uint8_t pd_destroy(const char *path)
{
    p_path1.count = 1;
    p_path1.path = topstr(pstr1, path);
    return pd_mli(MLI_DESTROY, &p_path1);
}

uint8_t pd_rename(const char *path, const char *newpath)
{
    p_rename.count = 2;
    p_rename.path = topstr(pstr1, path);
    p_rename.newpath = topstr(pstr2, newpath);
    return pd_mli(MLI_RENAME, &p_rename);
}

/* ---- open files --------------------------------------------------------- */

uint8_t pd_open(const char *path, uint8_t slot, uint8_t *ref)
{
    uint8_t err;
    p_open.count = 3;
    p_open.path = topstr(pstr1, path);
    p_open.iobuf = iobuf(slot);
    err = pd_mli(MLI_OPEN, &p_open);
    if (!err) *ref = p_open.ref;
    return err;
}

uint8_t pd_read(uint8_t ref, void *buf, uint16_t n, uint16_t *got)
{
    uint8_t err;
    p_rw.count = 4;
    p_rw.ref = ref;
    p_rw.buf = buf;
    p_rw.req = n;
    p_rw.got = 0;
    err = pd_mli(MLI_READ, &p_rw);
    *got = p_rw.got;
    return err;
}

uint8_t pd_write(uint8_t ref, const void *buf, uint16_t n)
{
    p_rw.count = 4;
    p_rw.ref = ref;
    p_rw.buf = (void *)buf;
    p_rw.req = n;
    p_rw.got = 0;
    return pd_mli(MLI_WRITE, &p_rw);
}

uint8_t pd_close(uint8_t ref)
{
    p_close.count = 1;
    p_close.ref = ref;
    return pd_mli(MLI_CLOSE, &p_close);
}

/* ---- errors / paths ----------------------------------------------------- */

const char *pd_errstr(uint8_t err)
{
    static char eb[18];

    switch (err) {
    case 0x27: return "I/O error";
    case 0x28: return "No device connected";
    case 0x2B: return "Disk write protected";
    case 0x44: case 0x45: case 0x46: return "Not found";
    case 0x47: return "File already exists";
    case 0x48: return "Disk full";
    case 0x49: return "Directory full";
    case 0x4E: case 0x50: return "Access denied or in use";
    }
    strcpy(eb, "ProDOS error $");
    eb[14] = "0123456789ABCDEF"[err >> 4];
    eb[15] = "0123456789ABCDEF"[err & 15];
    eb[16] = 0;
    return eb;
}

uint8_t pd_path_join(char *path, const char *name)
{
    uint8_t a = (uint8_t)strlen(path), b = (uint8_t)strlen(name);
    if ((unsigned)a + b + 1 > PD_PATH_MAX) return 0;
    path[a] = '/';
    memcpy(path + a + 1, name, (unsigned)b + 1);
    return 1;
}

void pd_path_parent(char *path)
{
    char *s = strrchr(path, '/');
    if (!s || s == path) { path[0] = 0; return; }
    *s = 0;
}
