/* Host test for src/bas.c with a stand-in Applesoft ROM keyword table. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../src/ovl/bas.h"

static const char *kw[] = {
 "END","FOR","NEXT","DATA","INPUT","DEL","DIM","READ","GR","TEXT","PR#","IN#","CALL","PLOT","HLIN","VLIN",
 "HGR2","HGR","HCOLOR=","HPLOT","DRAW","XDRAW","HTAB","HOME","ROT=","SCALE=","SHLOAD","TRACE","NOTRACE",
 "NORMAL","INVERSE","FLASH","COLOR=","POP","VTAB","HIMEM:","LOMEM:","ONERR","RESUME","RECALL","STORE",
 "SPEED=","LET","GOTO","RUN","IF","RESTORE","&","GOSUB","RETURN","REM","STOP","ON","WAIT","LOAD","SAVE",
 "DEF","POKE","PRINT","CONT","LIST","CLEAR","GET","NEW","TAB(","TO","FN","SPC(","THEN","AT","NOT","STEP",
 "+","-","*","/","^","AND","OR",">","=","<","SGN","INT","ABS","USR","FRE","SCRN(","PDL","POS","SQR","RND",
 "LOG","EXP","COS","SIN","TAN","ATN","PEEK","LEN","STR$","VAL","ASC","CHR$","LEFT$","RIGHT$","MID$" };

static uint8_t scratch[BAS_SCRATCH];
static uint8_t rom[512];

void bas_rom_table(uint8_t *dst) { memcpy(dst, rom, 512); }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static int tokenize(const char *line, uint8_t *out)
{
    return bas_tok_line(line, out);
}

int main(void)
{
    uint8_t o[260], i;
    char text[300];
    uint16_t n, pos = 0;

    for (i = 0; i < 107; ++i) {
        const char *k = kw[i];
        while (*k) { rom[pos] = (uint8_t)*k; if (!k[1]) rom[pos] |= 0x80; ++pos; ++k; }
    }
    CHECK(pos < 512);
    CHECK(bas_init(scratch) == 0);

    /* the a2chat fixture: 10 PRINT "HI" -> link,10,BA,"HI",0 (spaces dropped as Applesoft does) */
    n = tokenize("10 PRINT \"HI\"", o);
    CHECK(n == 10 && o[2] == 10 && o[3] == 0 && o[4] == 0xBA && o[5] == '"' && o[6] == 'H' && o[7] == 'I' && o[8] == '"' && o[9] == 0);
    n = tokenize("20 END", o);
    CHECK(n == 6 && o[4] == 0x80 && o[5] == 0);

    /* case, spaces, ? shortcut, GOTO, string kept literally */
    n = tokenize("30 for i = 1 to 10: ? \"a b\"; i: next i", o);
    CHECK(o[4] == 0x81 && o[5] == 'I' && o[6] == 0xD0 && o[7] == '1' && o[8] == 0xC1);
    CHECK(o[9] == '1' && o[10] == '0' && o[11] == ':' && o[12] == 0xBA && o[13] == '"' && o[14] == 'a' && o[16] == 'b');

    /* ATN is not AT + N; REM keeps its text; DATA runs to the colon */
    n = tokenize("40 A=ATN(1)", o);
    CHECK(o[4] == 'A' && o[5] == 0xD0 && o[6] == 0xE1 && o[7] == '(' );
    n = tokenize("50 REM hello there", o);
    CHECK(o[4] == 0xB2 && o[5] == ' ' && o[6] == 'h' && o[n - 2] == 'e');   /* text after REM is kept as typed */
    n = tokenize("60 DATA 1, 2,\"x:y\" : PRINT", o);
    CHECK(o[4] == 0x83 && o[5] == ' ' && o[6] == '1' && o[7] == ',' && o[n - 2] == 0xBA);

    /* blank / unnumbered lines are skipped */
    CHECK(tokenize("", o) == 0);
    CHECK(tokenize("PRINT 1", o) == 0);

    /* lister: LIST format, with the spaces Applesoft adds around keywords */
    n = tokenize("10 PRINT \"HI\"", o);
    n = bas_detok_line(10, o + 4, text, sizeof text);
    text[n] = 0;
    CHECK(!strcmp(text, "10  PRINT \"HI\""));
    n = tokenize("30 FOR I=1 TO 10", o);
    n = bas_detok_line(30, o + 4, text, sizeof text);
    text[n] = 0;
    CHECK(!strcmp(text, "30  FOR I = 1 TO 10"));
    n = tokenize("70 REM FOR LOOP", o);
    n = bas_detok_line(70, o + 4, text, sizeof text);
    text[n] = 0;
    CHECK(!strcmp(text, "70  REM  FOR LOOP"));
    n = tokenize("80 DATA A,B:PRINT", o);
    n = bas_detok_line(80, o + 4, text, sizeof text);
    text[n] = 0;
    CHECK(!strcmp(text, "80  DATA  A,B: PRINT "));
    CHECK(bas_detok_line(10, o + 4, text, 8) == 0xFFFF);

    /* round trip: listing re-tokenizes to the same bytes */
    {
        const char *progs[] = { "10 HOME: VTAB 5: PRINT \"HELLO, WORLD\"", "20 IF A>=3 AND B<>2 THEN GOTO 100",
                                "30 X=SQR(Y)*2^3-ABS(Z)/4", "40 DEF FN F(X)=X*X", "50 PRINT TAB(5);\"X\";SPC(2);CHR$(65)" };
        uint8_t o2[260];
        uint16_t n2, k;
        for (k = 0; k < 5; ++k) {
            n = tokenize(progs[k], o);
            n2 = bas_detok_line((uint16_t)atoi(progs[k]), o + 4, text, sizeof text);
            text[n2] = 0;
            n2 = tokenize(text, o2);
            CHECK(n == n2 && !memcmp(o, o2, n));
            if (n != n2 || memcmp(o, o2, n)) printf("  round trip: %s -> %s\n", progs[k], text);
        }
    }

    printf(fails ? "%d FAILED\n" : "all bas tests passed\n", fails);
    return fails != 0;
}
