#include <string.h>
#include "names.h"

static const char hexd[] = "0123456789ABCDEF";

static int8_t hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static char upc(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return c;
}

static uint8_t is_alpha(char c) { return c >= 'A' && c <= 'Z'; }
static uint8_t is_digit(char c) { return c >= '0' && c <= '9'; }

struct ext_map {
    const char *ext;
    uint8_t type;
    uint16_t aux;
    uint8_t strip;                  /* 1: the extension only says what the type already says */
};

static const struct ext_map exts[] = {
    { "TXT", PT_TXT, 0x0000, 1 }, { "TEXT", PT_TXT, 0x0000, 1 },
    { "MD",  PT_TXT, 0x0000, 0 }, { "S",    PT_TXT, 0x0000, 0 },
    { "C",   PT_TXT, 0x0000, 0 }, { "H",    PT_TXT, 0x0000, 0 },
    { "BAS", PT_BAS, 0x0801, 1 }, { "SYSTEM", PT_SYS, 0x2000, 0 },
    { "SYS", PT_SYS, 0x2000, 0 }, { "BIN",  PT_BIN, 0x0800, 1 },
    { "PO",  PT_BIN, 0x0800, 0 }, { "DO",   PT_BIN, 0x0800, 0 },
    { "DSK", PT_BIN, 0x0800, 0 }, { "HDV",  PT_BIN, 0x0800, 0 },
    { "2MG", PT_BIN, 0x0800, 0 },
    { 0, 0, 0, 0 }
};

/* The extension of host[0..len) (text after the last '.') if it is one we know, else NULL.
   *dot is set to where that extension's '.' is. Words like "ipsum" in LOREM.IPSUM are not extensions. */
static const struct ext_map *ext_of(const char *host, uint8_t len, uint8_t *dot)
{
    uint8_t i, d = 0;
    const struct ext_map *e;
    const char *p, *q;

    for (i = 0; i < len; ++i)
        if (host[i] == '.') d = (uint8_t)(i + 1);
    if (!d) return 0;
    for (e = exts; e->ext; ++e) {
        p = host + d;
        q = e->ext;
        while (p < host + len && *q && upc(*p) == *q) { ++p; ++q; }
        if (p == host + len && *q == 0) { *dot = (uint8_t)(d - 1); return e; }
    }
    return 0;
}

uint8_t names_host_to_prodos(const char *host, char *pname, uint8_t *type, uint16_t *aux)
{
    uint8_t len, i, n, has_suffix = 0, dot = 0;
    int8_t h;
    uint8_t t = 0;
    uint16_t a = 0;
    const struct ext_map *e;
    char c;

    len = (uint8_t)strlen(host);

    /* #TTAAAA suffix on files written by older versions: exactly six hex digits at the end */
    if (len > 7 && host[len - 7] == '#') {
        has_suffix = 1;
        for (i = 0; i < 6; ++i)
            if (hexval(host[len - 6 + i]) < 0) has_suffix = 0;
        if (has_suffix) {
            for (i = 0; i < 2; ++i) t = (uint8_t)((t << 4) | hexval(host[len - 6 + i]));
            for (i = 2; i < 6; ++i) a = (uint16_t)((a << 4) | hexval(host[len - 6 + i]));
            len -= 7;
        }
    }

    if (!has_suffix) {
        t = PT_BIN;
        a = 0x0800;
        e = ext_of(host, len, &dot);
        if (e) {
            t = e->type; a = e->aux;
            if (e->strip) len = dot;                  /* .txt / .bas / .bin: the type says it */
        } else if (!memchr(host, '.', len)) {         /* no extension at all: plain text, e.g. "shakespeare" */
            t = PT_TXT;
            a = 0;
        }
    }

    /* sanitize: letters, digits, '.', uppercase, must start with a letter */
    n = 0;
    for (i = 0; i < len && n < PD_NAME_MAX; ++i) {
        c = upc(host[i]);
        if (!is_alpha(c) && !is_digit(c) && c != '.') c = '.';
        if (n == 0 && !is_alpha(c)) pname[n++] = 'X';
        if (n < PD_NAME_MAX) pname[n++] = c;
    }
    if (n == 0) pname[n++] = 'X';
    pname[n] = 0;

    *type = t;
    *aux = a;
    h = has_suffix;
    return (uint8_t)h;
}

void names_prodos_to_host(const char *pname, uint8_t type, uint16_t aux, char *host)
{
    uint8_t n = (uint8_t)strlen(pname), i, dot, want;
    const struct ext_map *e;
    const char *add;

    (void)aux;
    memcpy(host, pname, n);
    if (type != PT_DIR) {
        for (i = 0; i < n; ++i)                  /* file names on the share are lower case */
            if (host[i] >= 'A' && host[i] <= 'Z') host[i] = (char)(host[i] + 32);
        /* Text is .txt, BASIC is a .bas listing, programs are .system, everything else is a plain
           .bin. A real extension (.po, .dsk ...) is kept; a dotted word such as LOREM.IPSUM is
           not one. The exact ProDOS type and aux type are not kept on the share. */
        want = type == PT_TXT ? PT_TXT : type == PT_BAS ? PT_BAS : type == PT_SYS ? PT_SYS : PT_BIN;
        add = want == PT_TXT ? ".txt" : want == PT_BAS ? ".bas" : want == PT_SYS ? ".system" : ".bin";
        e = ext_of(host, n, &dot);
        if (!e || e->type != want) { strcpy(host + n, add); n = (uint8_t)(n + strlen(add)); }
    }
    host[n] = 0;
}

void names_type_str(uint8_t type, char *out)
{
    const char *s = 0;
    switch (type) {
    case 0x00: s = "UNK"; break;
    case PT_TXT: s = "TXT"; break;
    case PT_BIN: s = "BIN"; break;
    case PT_FOT: s = "FOT"; break;
    case PT_DIR: s = "DIR"; break;
    case 0xB3: s = "S16"; break;
    case 0xFA: s = "INT"; break;
    case 0xFB: s = "IVR"; break;
    case 0xFC: s = "BAS"; break;
    case 0xFD: s = "VAR"; break;
    case 0xFE: s = "REL"; break;
    case PT_SYS: s = "SYS"; break;
    }
    if (s) {
        strcpy(out, s);
    } else {
        out[0] = '$';
        out[1] = hexd[type >> 4];
        out[2] = hexd[type & 15];
        out[3] = 0;
    }
}
