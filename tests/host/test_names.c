#include <stdio.h>
#include <string.h>
#include "../../src/names.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main(void)
{
    char p[PD_NAME_MAX + 1], h[PD_NAME_MAX + 8], t[4];
    uint8_t ty;
    uint16_t ax;

    CHECK(names_host_to_prodos("ARKANOID#062000", p, &ty, &ax) == 1);
    CHECK(!strcmp(p, "ARKANOID") && ty == 0x06 && ax == 0x2000);

    CHECK(names_host_to_prodos("notes.txt", p, &ty, &ax) == 0);
    CHECK(!strcmp(p, "NOTES") && ty == PT_TXT && ax == 0);     /* .txt is implied by the type */

    CHECK(names_host_to_prodos("Hello World.bas", p, &ty, &ax) == 0);
    CHECK(!strcmp(p, "HELLO.WORLD") && ty == PT_BAS && ax == 0x0801);      /* .bas is implied by the type */

    CHECK(names_host_to_prodos("1st-file.dat", p, &ty, &ax) == 0);
    CHECK(!strcmp(p, "X1ST.FILE.DAT") && ty == PT_BIN && ax == 0x0800);   /* unknown extension */

    CHECK(names_host_to_prodos("shakespeare", p, &ty, &ax) == 0);      /* no extension: text */
    CHECK(!strcmp(p, "SHAKESPEARE") && ty == PT_TXT && ax == 0);

    CHECK(names_host_to_prodos("A_very_long_file_name_here.dat", p, &ty, &ax) == 0);
    CHECK(strlen(p) == PD_NAME_MAX);

    CHECK(names_host_to_prodos("odd#12345", p, &ty, &ax) == 0);   /* only 5 hex digits */
    CHECK(!strcmp(p, "ODD.12345"));

    CHECK(names_host_to_prodos("x#zz0000", p, &ty, &ax) == 0);    /* not hex */

    CHECK(names_host_to_prodos("pic#08c000", p, &ty, &ax) == 1);  /* lower-case hex */
    CHECK(ty == 0x08 && ax == 0xC000);

    names_prodos_to_host("SPREADSHEET", 0x06, 0x4000, h);
    CHECK(!strcmp(h, "spreadsheet.bin"));                         /* not text: a plain binary */
    names_prodos_to_host("SHAKESPEARE", PT_TXT, 0, h);
    CHECK(!strcmp(h, "shakespeare.txt"));                         /* text: .txt */
    names_prodos_to_host("LOREM.IPSUM", PT_TXT, 0, h);
    CHECK(!strcmp(h, "lorem.ipsum.txt"));                         /* "ipsum" is not an extension */
    names_prodos_to_host("NOTES.TXT", PT_TXT, 0, h);
    CHECK(!strcmp(h, "notes.txt"));                               /* a real .txt is not doubled */
    names_prodos_to_host("README.MD", PT_TXT, 0, h);
    CHECK(!strcmp(h, "readme.md"));
    names_prodos_to_host("HELLO.WORLD", PT_BAS, 0x0801, h);
    CHECK(!strcmp(h, "hello.world.bas"));
    names_prodos_to_host("SMBGET.SYSTEM", PT_SYS, 0x2000, h);
    CHECK(!strcmp(h, "smbget.system"));
    names_prodos_to_host("STARTUP", PT_SYS, 0x2000, h);
    CHECK(!strcmp(h, "startup.system"));
    names_prodos_to_host("SMBGET.OVL", PT_BIN, 0x0800, h);
    CHECK(!strcmp(h, "smbget.ovl.bin"));                          /* ".ovl" maps to nothing: add .bin */
    names_prodos_to_host("MONARCH", PT_FOT, 0x4001, h);
    CHECK(!strcmp(h, "monarch.bin"));
    names_prodos_to_host("JESU.JOY", 0xD5, 0x0E7, h);
    CHECK(!strcmp(h, "jesu.joy.bin"));
    names_prodos_to_host("A2STREAM.PO", PT_BIN, 0, h);
    CHECK(!strcmp(h, "a2stream.po"));                             /* .po is a real extension */
    names_prodos_to_host("MUSIC", PT_DIR, 0, h);
    CHECK(!strcmp(h, "MUSIC"));

    /* and back: the added extension is dropped again */
    CHECK(names_host_to_prodos("lorem.ipsum.txt", p, &ty, &ax) == 0);
    CHECK(!strcmp(p, "LOREM.IPSUM") && ty == PT_TXT && ax == 0);
    CHECK(names_host_to_prodos("smbget.ovl.bin", p, &ty, &ax) == 0);
    CHECK(!strcmp(p, "SMBGET.OVL") && ty == PT_BIN && ax == 0x0800);
    CHECK(names_host_to_prodos("monarch.bin", p, &ty, &ax) == 0);
    CHECK(!strcmp(p, "MONARCH") && ty == PT_BIN);
    CHECK(names_host_to_prodos("a2stream.po", p, &ty, &ax) == 0);
    CHECK(!strcmp(p, "A2STREAM.PO") && ty == PT_BIN);
    CHECK(names_host_to_prodos("lorem.ipsum", p, &ty, &ax) == 0);  /* unknown extension: BIN, sniffed as text on download */
    CHECK(ty == PT_BIN && ax == 0x0800);

    names_type_str(PT_SYS, t); CHECK(!strcmp(t, "SYS"));
    names_type_str(0x42, t);   CHECK(!strcmp(t, "$42"));

    printf(fails ? "%d FAILED\n" : "all names tests passed\n", fails);
    return fails != 0;
}
