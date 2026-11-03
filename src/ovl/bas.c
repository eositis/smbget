#include <string.h>
#include "bas.h"

void __fastcall__ bas_rom_table(uint8_t *dst);

#define NTOK  107
#define T_DATA 0x83
#define T_PRINT 0xBA
#define T_REM  0xB2
#define T_AT   0xC5

/* scratch layout inside tab: [0..511] ROM table, then the tables below */
static uint8_t *tab;
static uint16_t *idx;               /* NTOK: offset of each keyword in tab */
static uint8_t *lst;                /* NTOK: token numbers grouped by first character */
static uint8_t *grp;                /* 28: start of each group in lst (26 letters + symbols + end) */

static uint8_t group(char c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
    return 26;
}

uint8_t bas_init(uint8_t *t)
{
    uint8_t i, g, n;
    uint16_t pos = 0;

    tab = t;
    idx = (uint16_t *)(t + 512);
    lst = (uint8_t *)(idx + NTOK);
    grp = lst + NTOK;
    bas_rom_table(tab);
    if (tab[0] != 'E' || tab[1] != 'N' || tab[2] != ('D' | 0x80)) return 1;
    for (i = 0; i < NTOK; ++i) {
        idx[i] = pos;
        while (pos < 512 && !(tab[pos] & 0x80)) ++pos;
        if (++pos > 512) return 1;
    }
    /* group the tokens by first character, keeping table order inside a group */
    n = 0;
    for (g = 0; g < 27; ++g) {
        grp[g] = n;
        for (i = 0; i < NTOK; ++i)
            if (group((char)(tab[idx[i]] & 0x7F)) == g) lst[n++] = i;
    }
    grp[27] = n;
    return 0;
}

/* ---- tokenizer ------------------------------------------------------------ */

static char upc(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return c;
}

/* Find the keyword at p (spaces between its letters are ignored, as in Applesoft).
   Returns the token number 0x80.. or 0; *end is set past the match. */
static uint8_t match(const char *p, const char **end)
{
    uint8_t g, i, t;
    const uint8_t *k;
    const char *q;
    char c, d;

    g = group(upc(*p));
    for (i = grp[g]; i < grp[g + 1]; ++i) {
        t = lst[i];
        k = tab + idx[t];
        q = p;
        for (;;) {
            c = (char)(*k & 0x7F);
            while (*q == ' ') ++q;
            d = upc(*q);
            if (c != d) break;
            ++q;
            if (*k++ & 0x80) {                       /* whole keyword matched */
                if (t == (uint8_t)(T_AT - 0x80)) {   /* "AT" followed by N is ATN */
                    const char *r = q;
                    while (*r == ' ') ++r;
                    if (upc(*r) == 'N') break;
                }
                *end = q;
                return (uint8_t)(t + 0x80);
            }
        }
    }
    return 0;
}

uint16_t bas_tok_line(const char *p, uint8_t *out)
{
    uint16_t n = 0, o;
    uint8_t t, inq;
    const char *e;
    char c;

    while (*p == ' ') ++p;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') n = (uint16_t)(n * 10 + (*p++ - '0'));
    out[0] = out[1] = 0;
    out[2] = (uint8_t)n;
    out[3] = (uint8_t)(n >> 8);
    o = 4;
    while ((c = *p) != 0) {
        if (c == ' ') { ++p; continue; }
        if (c == '"') {                               /* string: copy through the closing quote */
            out[o++] = (uint8_t)c;
            ++p;
            while (*p && *p != '"') out[o++] = (uint8_t)*p++;
            if (*p) out[o++] = (uint8_t)*p++;
            continue;
        }
        c = upc(c);
        if (c == '?') { out[o++] = T_PRINT; ++p; continue; }
        t = 0;
        if ((c >= 'A' && c <= 'Z') || (c != '"' && (c == '&' || c == '+' || c == '-' || c == '*'
                                 || c == '/' || c == '^' || c == '<' || c == '>' || c == '=')))
            t = match(p, &e);
        if (!t) { out[o++] = (uint8_t)c; ++p; continue; }
        out[o++] = t;
        p = e;
        if (t == T_REM) {                             /* the rest of the line is a comment */
            while (*p) out[o++] = (uint8_t)*p++;
            break;
        }
        if (t == T_DATA) {                            /* DATA: raw up to the next ':' outside quotes */
            inq = 0;
            while (*p && (inq || *p != ':')) {
                if (*p == '"') inq ^= 1;
                out[o++] = (uint8_t)*p++;
            }
        }
    }
    out[o++] = 0;
    return o;
}

/* ---- lister ----------------------------------------------------------------- */

uint16_t bas_detok_line(uint16_t lineno, const uint8_t *s, char *out, uint16_t room)
{
    char num[6];
    uint8_t i = 0, c, inq = 0, raw = 0;
    const uint8_t *k;
    uint16_t o = 0;

    do { num[i++] = (char)('0' + lineno % 10); lineno /= 10; } while (lineno);
    if (room < 12) return 0xFFFF;
    while (i) out[o++] = num[--i];
    out[o++] = ' ';
    while ((c = *s++) != 0) {
        if (o + 12 > room) return 0xFFFF;
        if (c == '"') inq ^= 1;
        if (raw == 2 && c == ':' && !inq) raw = 0;     /* DATA text ends at the colon */
        if (c < 0x80 || raw || inq) {
            out[o++] = (char)c;
            continue;
        }
        k = tab + idx[c - 0x80];
        out[o++] = ' ';
        do {
            out[o++] = (char)(*k & 0x7F);
        } while (!(*k++ & 0x80));
        out[o++] = ' ';
        if (c == T_REM) raw = 1;                       /* REM / DATA text is copied as typed */
        else if (c == T_DATA) raw = 2;
    }
    return o;
}
