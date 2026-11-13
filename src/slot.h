#ifndef SLOT_H
#define SLOT_H

#include <stdint.h>

/* Disk II slot ROM. Its $C0nX switches move the head. */
uint8_t slot_is_disk2(uint8_t s);

/* 4 when this IIc has MegaFlash, else 0. The virtual W5100 shares slot 4. */
uint8_t slot_megaflash(void);

/* Read-only CoGS ID, slots 7..1. Skips Disk II and the MegaFlash slot.
 * configured 1..7 checks that slot only. Returns 0 if none. */
uint8_t slot_find_cogs(uint8_t configured);

/* IIgs and IIc: slot 4. Enhanced IIe: slot 3. */
uint8_t slot_prefer_uther(void);

#endif
