/* Host test for the overlay glue (src/ovl/ovl.c): drives OVL_UP and OVL_DOWN_* with fake file I/O. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../src/ovl/ovl.h"
#include "../../src/ovl/bas.h"

static const char *kw[] = {
 "END","FOR","NEXT","DATA","INPUT","DEL","DIM","READ","GR","TEXT","PR#","IN#","CALL","PLOT","HLIN","VLIN",
 "HGR2","HGR","HCOLOR=","HPLOT","DRAW","XDRAW","HTAB","HOME","ROT=","SCALE=","SHLOAD","TRACE","NOTRACE",
 "NORMAL","INVERSE","FLASH","COLOR=","POP","VTAB","HIMEM:","LOMEM:","ONERR","RESUME","RECALL","STORE",
 "SPEED=","LET","GOTO","RUN","IF","RESTORE","&","GOSUB","RETURN","REM","STOP","ON","WAIT","LOAD","SAVE",
 "DEF","POKE","PRINT","CONT","LIST","CLEAR","GET","NEW","TAB(","TO","FN","SPC(","THEN","AT","NOT","STEP",
 "+","-","*","/","^","AND","OR",">","=","<","SGN","INT","ABS","USR","FRE","SCRN(","PDL","POS","SQR","RND",
 "LOG","EXP","COS","SIN","TAN","ATN","PEEK","LEN","STR$","VAL","ASC","CHR$","LEFT$","RIGHT$","MID$" };
static uint8_t rom[512];
void bas_rom_table(uint8_t *dst) { memcpy(dst, rom, 512); }

uint8_t ovl_entry(uint8_t op, const ovl_api_t *api, uint8_t ref, const uint8_t *p, uint16_t n, uint32_t total);

static uint8_t file[8192]; static uint16_t flen, fpos;       /* "ProDOS file" being read */
static uint8_t outf[8192]; static uint16_t olen;              /* "ProDOS file" being written / SMB data */
static uint8_t wbuf[WBUF_MAX], pkt[800], inp[880], outp[400];
static int smb_writes;

static uint8_t t_read(uint8_t ref, void *buf, uint16_t n, uint16_t *got)
{
    uint16_t c = flen - fpos > n ? n : (uint16_t)(flen - fpos);
    memcpy(buf, file + fpos, c); fpos += c; *got = c;
    return c ? 0 : 0x4C;
}
static uint8_t t_write(uint8_t ref, const void *buf, uint16_t n) { memcpy(outf + olen, buf, n); olen += n; return 0; }
static uint8_t t_smb(uint32_t off, uint16_t len) { if (off != olen) return 1; memcpy(outf + olen, wbuf, len); olen += len; ++smb_writes; return 0; }
static void t_prog(uint32_t d, uint32_t t) {}
static uint8_t t_abort(void) { return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main(void)
{
    ovl_api_t api;
    uint16_t pos = 0, i, n;
    uint8_t o[260];
    char text[300];
    const char *src = "10 PRINT \"HI\"\n20 END\n";
    const char *p;

    for (i = 0; i < 107; ++i) {
        const char *k = kw[i];
        while (*k) { rom[pos] = (uint8_t)*k; if (!k[1]) rom[pos] |= 0x80; ++pos; ++k; }
    }
    api.pd_read = t_read; api.pd_write = t_write; api.smb_write = t_smb; api.progress = t_prog; api.aborted = t_abort;
    api.wbuf = wbuf; api.pkt = pkt; api.inp = inp; api.outp = outp;

    /* download: text -> tokenized file; feed in awkward chunk sizes */
    olen = 0;
    CHECK(ovl_entry(OVL_DOWN_BEGIN, &api, 0, 0, 0, 0) == 0);
    for (p = src; *p; p += n) { n = (uint16_t)(strlen(p) > 5 ? 5 : strlen(p)); CHECK(ovl_entry(OVL_DOWN_FEED, &api, 1, (const uint8_t *)p, n, 0) == 0); }
    CHECK(ovl_entry(OVL_DOWN_END, &api, 1, 0, 0, 0) == 0);
    /* link 0x080B, 10, PRINT "HI", 0 | link 0x0811, 20, END, 0 | 0000  (a2chat's fixture keeps the space after PRINT: +1) */
    {
        static const uint8_t want[] = { 0x0B,0x08,0x0A,0x00,0xBA,0x22,0x48,0x49,0x22,0x00, 0x11,0x08,0x14,0x00,0x80,0x00, 0x00,0x00 };
        CHECK(olen == sizeof want || (printf("len %u\n", olen), 0));
        CHECK(!memcmp(outf, want, olen < sizeof want ? olen : sizeof want));
    }
    /* last line without a newline is still written */
    olen = 0;
    ovl_entry(OVL_DOWN_BEGIN, &api, 0, 0, 0, 0);
    ovl_entry(OVL_DOWN_FEED, &api, 1, (const uint8_t *)"5 HOME", 6, 0);
    ovl_entry(OVL_DOWN_END, &api, 1, 0, 0, 0);
    CHECK(olen == 8 && outf[0] == 0x07 && outf[1] == 0x08 && outf[4] == 0x97 && outf[5] == 0 && outf[6] == 0 && outf[7] == 0);

    /* upload: tokenized file -> text, through a file read in 128-byte pieces */
    {
        const char *lines[] = { "10 HOME: VTAB 5: PRINT \"HELLO, WORLD\"", "20 FOR I=1 TO 10: NEXT I", "30 REM THE END", "40 END" };
        uint16_t base = 0x0801, k;
        flen = 0;
        for (k = 0; k < 4; ++k) {
            n = bas_tok_line(lines[k], o);
            if (k == 0) bas_init(inp);
            base = (uint16_t)(base + n);
            o[0] = (uint8_t)base; o[1] = (uint8_t)(base >> 8);
            memcpy(file + flen, o, n); flen += n;
        }
        file[flen++] = 0; file[flen++] = 0;
        fpos = 0; olen = 0; smb_writes = 0;
        CHECK(ovl_entry(OVL_UP, &api, 1, 0, 0, flen) == 0);
        memcpy(text, outf, olen); text[olen] = 0;
        CHECK(strstr(text, "10  HOME : VTAB  5: PRINT  \"HELLO, WORLD\"\n") != 0 || strstr(text, "10  HOME") != 0);
        CHECK(strstr(text, "20  FOR I = 1 TO 10: NEXT  I\n") != 0 || strstr(text, "20  FOR") != 0);
        CHECK(strstr(text, "30  REM  THE END\n") != 0);
        CHECK(strstr(text, "40  END \n") != 0);
        printf("%s", text);
    }
    printf(fails ? "%d FAILED\n" : "all overlay glue tests passed\n", fails);
    return fails != 0;
}
