#include <conio.h>
#include <apple2enh.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

void ui_init(void)
{
    videomode(VIDEOMODE_80COL);
    clrscr();
    cursor(0);
}

void ui_done(void)
{
    revers(0);
    cursor(1);
    clrscr();
}

void ui_hline(uint8_t x, uint8_t y, uint8_t w, char c)
{
    gotoxy(x, y);
    while (w--) cputc(c);
}

void ui_clear_row(uint8_t y)
{
    cclearxy(0, y, SCR_W - 1);
}

void ui_text(uint8_t x, uint8_t y, const char *s)
{
    cputsxy(x, y, s);
}

void ui_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const char *title)
{
    uint8_t i, t = (uint8_t)strlen(title);

    gotoxy(x, y);
    cputc('+');
    cputc('[');
    cputc(' ');
    cputs(title);
    cputc(' ');
    cputc(']');
    for (i = (uint8_t)(t + 5); i < w - 1; ++i) cputc('=');
    cputc('+');
    for (i = 1; i < h - 1; ++i) {
        gotoxy(x, (uint8_t)(y + i));
        cputc('|');
        for (t = 0; t < w - 2; ++t) cputc(' ');
        cputc('|');
    }
    gotoxy(x, (uint8_t)(y + h - 1));
    cputc('+');
    for (i = 1; i < w - 1; ++i) cputc('=');
    cputc('+');
}

void ui_msg(const char *s)
{
    ui_clear_row(ROW_MSG);
    cputsxy(1, ROW_MSG, s);
}

void ui_msg2(const char *a, const char *b)
{
    ui_clear_row(ROW_MSG);
    cputsxy(1, ROW_MSG, a);
    cputs(b);
}

/* dialog x/width derived from the longest line */
static uint8_t dlg_w(const char *title, const char *l1, const char *l2, uint8_t min)
{
    uint8_t w = min, n;
    n = (uint8_t)(strlen(title) + 8);
    if (n > w) w = n;
    n = (uint8_t)(strlen(l1) + 6);
    if (n > w) w = n;
    if (l2) {
        n = (uint8_t)(strlen(l2) + 6);
        if (n > w) w = n;
    }
    if (w > 76) w = 76;
    return w;
}

void ui_alert(const char *title, const char *l1, const char *l2)
{
    uint8_t w = dlg_w(title, l1, l2, 34), x = (uint8_t)((SCR_W - w) / 2);
    ui_box(x, 8, w, 7, title);
    cputsxy((uint8_t)(x + 3), 10, l1);
    if (l2) cputsxy((uint8_t)(x + 3), 11, l2);
    cputsxy((uint8_t)(x + 3), 13, "Press any key");
    cgetc();
}

uint8_t ui_confirm(const char *title, const char *l1, const char *l2)
{
    uint8_t w = dlg_w(title, l1, l2, 34), x = (uint8_t)((SCR_W - w) / 2);
    char c;
    ui_box(x, 8, w, 7, title);
    cputsxy((uint8_t)(x + 3), 10, l1);
    if (l2) cputsxy((uint8_t)(x + 3), 11, l2);
    cputsxy((uint8_t)(x + 3), 13, "[Y]es   [N]o");
    for (;;) {
        c = cgetc();
        if (c == 'y' || c == 'Y') return 1;
        if (c == 'n' || c == 'N' || c == CH_ESC) return 0;
    }
}

uint8_t ui_input(const char *title, const char *prompt, char *buf, uint8_t maxlen)
{
    uint8_t w = 50, x = (uint8_t)((SCR_W - w) / 2), len = (uint8_t)strlen(buf);
    char c;

    ui_box(x, 8, w, 7, title);
    cputsxy((uint8_t)(x + 3), 10, prompt);
    cputsxy((uint8_t)(x + 3), 13, "RET = OK   ESC = cancel");
    cursor(1);
    for (;;) {
        cclearxy((uint8_t)(x + 3), 11, (uint8_t)(maxlen + 1));
        cputsxy((uint8_t)(x + 3), 11, buf);
        c = cgetc();
        if (c == CH_ENTER) { cursor(0); return 1; }
        if (c == CH_ESC) { cursor(0); return 0; }
        if (c == CH_DEL || c == CH_CURS_LEFT) {
            if (len) buf[--len] = 0;
        } else if (c >= 32 && c < 127 && len < maxlen) {
            buf[len++] = c;
            buf[len] = 0;
        }
    }
}

/* ---- numbers / dates --------------------------------------------------- */

void fmt_num(char *dst, uint32_t v, uint8_t w)
{
    char tmp[12];
    uint8_t n, i;

    if (v > 999999UL) {
        ultoa(v >> 10, tmp, 10);
        n = (uint8_t)strlen(tmp);
        tmp[n] = 'K';
        tmp[n + 1] = 0;
    } else {
        ultoa(v, tmp, 10);
    }
    n = (uint8_t)strlen(tmp);
    for (i = 0; i < w; ++i) dst[i] = ' ';
    if (n > w) n = w;
    memcpy(dst + (w - n), tmp, n);
    dst[w] = 0;
}

static const char months[] = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";

void fmt_date(char *dst, uint16_t d)
{
    uint8_t day = d & 31, mon = (d >> 5) & 15, yr = (d >> 9) & 127;

    memset(dst, ' ', 9);
    dst[9] = 0;
    if (d == 0 || mon < 1 || mon > 12) return;
    dst[0] = (char)('0' + day / 10);
    dst[1] = (char)('0' + day % 10);
    dst[2] = '-';
    memcpy(dst + 3, months + (mon - 1) * 3, 3);
    dst[6] = '-';
    dst[7] = (char)('0' + (yr % 100) / 10);
    dst[8] = (char)('0' + yr % 10);
}

/* ---- pickers ------------------------------------------------------------ */

static void fmt_size_k(char *dst, uint16_t blocks)    /* 6 chars, e.g. "  800K" */
{
    uint32_t k = blocks / 2u;
    char tmp[8];
    uint8_t n;
    if (k >= 1024) { ultoa(k / 1024, tmp, 10); strcat(tmp, "M"); }
    else           { ultoa(k, tmp, 10);        strcat(tmp, "K"); }
    n = (uint8_t)strlen(tmp);
    memset(dst, ' ', 6);
    memcpy(dst + 6 - n, tmp, n);
    dst[6] = 0;
}

int8_t ui_pick_volume(const vol_t *v, uint8_t n, uint8_t cur)
{
    uint8_t x = 2, y = 3, w = 40, i;
    char line[40], num[8];
    char c;

    if (cur >= n) cur = 0;
    ui_box(x, y, w, (uint8_t)(n + 5), "Select Volume");
    cputsxy((uint8_t)(x + 2), (uint8_t)(y + n + 3), "Arrows+RET select  ESC cancel");
    for (;;) {
        for (i = 0; i < n; ++i) {
            uint8_t slot = (v[i].unit >> 4) & 7, drv = (v[i].unit & 0x80) ? 2 : 1;
            memset(line, ' ', 36);
            line[36] = 0;
            line[0] = (char)(i == cur ? '>' : ' ');
            line[1] = 'S'; line[2] = (char)('0' + slot);
            line[3] = ','; line[4] = 'D'; line[5] = (char)('0' + drv);
            line[7] = '/';
            memcpy(line + 8, v[i].name, strlen(v[i].name));
            fmt_size_k(num, v[i].total);
            memcpy(line + 24, num, 6);
            fmt_num(num, (uint32_t)(v[i].total - v[i].used), 5);
            memcpy(line + 30, num, 5);
            revers(i == cur);
            cputsxy((uint8_t)(x + 2), (uint8_t)(y + 1 + i), line);
            revers(0);
        }
        c = cgetc();
        if (c == CH_ESC) return -1;
        if (c == CH_ENTER) return (int8_t)cur;
        if ((c == CH_CURS_UP || c == CH_CURS_LEFT) && cur > 0) --cur;
        if ((c == CH_CURS_DOWN || c == CH_CURS_RIGHT) && cur + 1 < n) ++cur;
    }
}

char ui_overwrite(const char *name)
{
    uint8_t w = dlg_w("File exists", name, 0, 52), x = (uint8_t)((SCR_W - w) / 2);
    char c;
    ui_box(x, 8, w, 8, "File exists");
    cputsxy((uint8_t)(x + 3), 10, name);
    cputsxy((uint8_t)(x + 3), 11, "already exists on the target.");
    cputsxy((uint8_t)(x + 3), 13, "[O]verwrite [S]kip O[A]ll S[N]one [C]ancel");
    for (;;) {
        c = cgetc();
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c == CH_ESC) c = 'C';
        if (c == 'O' || c == 'S' || c == 'A' || c == 'N' || c == 'C') return c;
    }
}
