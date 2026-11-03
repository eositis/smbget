#include <conio.h>
#include <string.h>
#include "pane.h"
#include "ui.h"
#include "smb2.h"

static void vol_free(pane_t *p)
{
    char vol[PD_NAME_MAX + 2];
    finfo_t fi;
    uint8_t i = 1;

    p->free_blocks = 0;
    if (p->path[0] != '/') return;
    vol[0] = '/';
    while (p->path[i] && p->path[i] != '/' && i <= PD_NAME_MAX) { vol[i] = p->path[i]; ++i; }
    vol[i] = 0;
    if (!pd_getinfo(vol, &fi)) p->free_blocks = fi.aux - fi.blocks;
}

void pane_get(const pane_t *p, uint16_t idx, fentry_t *out)
{
    aux_read((uint16_t)(p->auxbase + idx * sizeof(fentry_t)), out, sizeof(fentry_t));
}

void pane_put(const pane_t *p, uint16_t idx, const fentry_t *f)
{
    aux_write((uint16_t)(p->auxbase + idx * sizeof(fentry_t)), f, sizeof(fentry_t));
}

void pane_hostname(const pane_t *p, const fentry_t *f, char *out)
{
    if (p->kind == PK_SMB && f->hname != NAME_NONE) {
        aux_read((uint16_t)(p->heapbase + f->hname), out, 80);
        out[79] = 0;
    } else {
        strcpy(out, f->name);
    }
}

static pane_t *load_pane;
static uint16_t load_n;

static uint8_t load_cb(const fentry_t *f)
{
    fentry_t e;
    memcpy(&e, f, sizeof e);
    e.hname = NAME_NONE;
    if (load_pane->kind == PK_SMB && dir_hname) {
        uint8_t n = (uint8_t)(strlen(dir_hname) + 1);
        if (load_pane->heapused + n <= NAMEHEAP) {
            aux_write((uint16_t)(load_pane->heapbase + load_pane->heapused), dir_hname, n);
            e.hname = load_pane->heapused;
            load_pane->heapused = (uint16_t)(load_pane->heapused + n);
        }
    }
    pane_put(load_pane, load_n++, &e);
    if (load_n >= MAX_ENTRIES) { load_pane->truncated = 1; return 1; }
    return 0;
}

uint8_t pane_load(pane_t *p)
{
    uint8_t err = 0;
    fentry_t up;

    p->count = p->top = p->cur = p->tagged = 0;
    p->truncated = 0;
    p->loaded = 0;
    p->heapused = 0;
    if (p->kind == PK_SMB && !smb_connected()) return 0;

    memset(&up, 0, sizeof up);
    strcpy(up.name, "..");
    up.flags = F_UP | F_DIR;
    pane_put(p, 0, &up);
    load_pane = p;
    load_n = 1;
    if (p->kind == PK_SMB) {
        err = smb_readdir(p->path, load_cb);
        if (err) err = (uint8_t)(err + 0x80);
    } else {
        err = pd_readdir(p->path, load_cb);
    }
    p->count = load_n;
    p->loaded = (uint8_t)(err == 0);
    if (p->kind == PK_LOCAL) vol_free(p);
    return err;
}

/* Build one 38-column row. */
static void build_row(char *s, const fentry_t *f)
{
    char tmp[12];

    memset(s, ' ', 38);
    s[38] = 0;
    if (f->flags & F_TAG) s[0] = '*';
    memcpy(s + 1, f->name, strlen(f->name));
    if (f->flags & F_UP) {
        memcpy(s + 21, "  <UP>", 6);
        return;
    }
    names_type_str(f->type, tmp);
    memcpy(s + 17, tmp, 3);
    if (!(f->flags & F_DIR)) {
        fmt_num(tmp, f->blocks, 6);
        memcpy(s + 21, tmp, 6);
    }
    fmt_date(tmp, f->date);
    memcpy(s + 28, tmp, 9);
}

void pane_draw_row(const pane_t *p, uint16_t idx, uint8_t active)
{
    char row[40];
    fentry_t f;
    uint8_t y;

    if (idx < p->top || idx >= p->top + LIST_ROWS) return;
    y = (uint8_t)(ROW_LIST + (idx - p->top));
    gotoxy(p->x, y);
    cputc('|');
    if (idx < p->count) {
        pane_get(p, idx, &f);
        build_row(row, &f);
        if (idx == p->cur && active) revers(1);
        cputs(row);
        revers(0);
    } else {
        cclear(38);
    }
    cputc('|');
}

void pane_draw(const pane_t *p, uint8_t active)
{
    uint8_t i, n, w;
    char title[44];

    /* top border with title */
    gotoxy(p->x, ROW_PANE_TOP);
    cputc('+');
    title[0] = 0;
    if (p->kind == PK_LOCAL) {
        strcpy(title, "[ LOCAL ");
        n = (uint8_t)strlen(p->path);
        if (n > 26) { strcat(title, "..."); strcat(title, p->path + n - 23); }
        else strcat(title, p->path);
        strcat(title, " ]");
    } else {
        strcpy(title, "[ SMB ");
        n = (uint8_t)strlen(p->path);
        if (n > 26) { strcat(title, "..."); strcat(title, p->path + n - 23); }
        else strcat(title, p->path);
        strcat(title, " ]");
    }
    if (active) revers(1);
    cputs(title);
    revers(0);
    w = (uint8_t)strlen(title);
    for (i = (uint8_t)(w + 1); i < 39; ++i) cputc('-');
    cputc('+');

    for (i = 0; i < LIST_ROWS; ++i) pane_draw_row(p, (uint16_t)(p->top + i), active);

    gotoxy(p->x, ROW_PANE_BOT);
    cputc('+');
    for (i = 0; i < 38; ++i) cputc('-');
    cputc('+');

    if (p->kind == PK_SMB && !p->loaded) {
        cputsxy((uint8_t)(p->x + 4), ROW_LIST + 3, "Not connected: press N");
    }
}

void pane_move(pane_t *p, int16_t delta, uint8_t active)
{
    uint16_t old = p->cur, oldtop = p->top;
    int32_t n = (int32_t)p->cur + delta;

    if (p->count == 0) return;
    if (n < 0) n = 0;
    if (n >= (int32_t)p->count) n = p->count - 1;
    p->cur = (uint16_t)n;
    if (p->cur < p->top) p->top = p->cur;
    if (p->cur >= p->top + LIST_ROWS) p->top = (uint16_t)(p->cur - LIST_ROWS + 1);
    if (p->top != oldtop) {
        for (n = 0; n < LIST_ROWS; ++n) pane_draw_row(p, (uint16_t)(p->top + n), active);
    } else {
        pane_draw_row(p, old, active);
        pane_draw_row(p, p->cur, active);
    }
}

static fentry_t cur_ent;

fentry_t *pane_cur(pane_t *p)
{
    if (!p->count) return 0;
    pane_get(p, p->cur, &cur_ent);
    return &cur_ent;
}

void pane_tag(pane_t *p)
{
    fentry_t *f = pane_cur(p);
    if (!f || (f->flags & (F_UP | F_DIR))) return;
    f->flags ^= F_TAG;
    if (f->flags & F_TAG) ++p->tagged; else --p->tagged;
    pane_put(p, p->cur, f);
}

void pane_tag_all(pane_t *p)
{
    uint16_t i;
    uint8_t any = (uint8_t)(p->tagged != 0);

    p->tagged = 0;
    for (i = 0; i < p->count; ++i) {
        fentry_t f;
        pane_get(p, i, &f);
        if (f.flags & (F_UP | F_DIR)) continue;
        if (any) f.flags &= (uint8_t)~F_TAG;
        else { f.flags |= F_TAG; ++p->tagged; }
        pane_put(p, i, &f);
    }
}
