#include <apple2.h>
#include <stdint.h>
#include "slot.h"

unsigned char mf_present(void);

uint8_t slot_is_disk2(uint8_t s)
{
    const volatile uint8_t *rom;

    if (s < 1 || s > 7) {
        return 1;
    }
    rom = (const uint8_t *)(0xC000 | ((uint16_t)s << 8));
    return (uint8_t)(rom[1] == 0x20 && rom[3] == 0x00 && rom[5] == 0x03 &&
                     rom[0xFF] == 0x00);
}

/* $C0n5 = $A2, version nonzero, status not $FF. Reads only. */
static uint8_t cogs_id(uint8_t s)
{
    const volatile uint8_t *reg = (const uint8_t *)(0xC080 | ((uint16_t)s << 4));

    return (uint8_t)(reg[5] == 0xA2 && reg[6] != 0 && reg[1] != 0xFF);
}

uint8_t slot_megaflash(void)
{
    unsigned char rom;

    /* $FBB3 == 6 is a IIc. $FBC0 == $E0 is a IIe, where $C0C0 belongs to
     * whatever card is in slot 4. */
    if (*(volatile unsigned char *)0xFBB3 != 6 ||
        *(volatile unsigned char *)0xFBC0 == 0xE0) {
        return 0;
    }
    rom = *(volatile unsigned char *)0xFBBF;
    if (rom != 4 && rom != 5) {
        return 0;
    }
    if (!mf_present()) {
        return 0;
    }
    return 4;
}

uint8_t slot_find_cogs(uint8_t configured)
{
    uint8_t s;
    uint8_t mf;

    mf = slot_megaflash();
    if (configured >= 1 && configured <= 7) {
        if (configured != mf && !slot_is_disk2(configured) && cogs_id(configured)) {
            return configured;
        }
        return 0;
    }
    for (s = 7; s >= 1; s--) {
        if (s == mf || slot_is_disk2(s)) {
            continue;
        }
        if (cogs_id(s)) {
            return s;
        }
    }
    return 0;
}

uint8_t slot_prefer_uther(void)
{
    unsigned char ost = get_ostype();

    if (ost == APPLE_IIGS || (ost >= APPLE_IIC && ost < APPLE_IIGS)) {
        return 4;
    }
    return 3;
}
