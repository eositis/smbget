/* CoGS framed transport. cogs.c implements w5100_* on top of it.
 * Register primitives are in cogs_io.s. Every DATA access uses abs,X.
 */
#ifndef COGS_H
#define COGS_H

#include <stdint.h>

void __fastcall__ cogs_set_slot(uint8_t slot);
uint8_t __fastcall__ cogs_probe(uint8_t slot);
uint8_t __fastcall__ cogs_reg_rd(uint8_t reg);
void __fastcall__ cogs_reg_wr(uint8_t reg, uint8_t val);
uint16_t cogs_avail(void);
int cogs_get(void);
uint8_t __fastcall__ cogs_getn(uint8_t *dst, uint8_t n);
uint8_t __fastcall__ cogs_skip(uint16_t n);
uint8_t __fastcall__ cogs_put(uint8_t b);
uint8_t __fastcall__ cogs_putn(const uint8_t *src, uint16_t n);
uint8_t cogs_resync(void);
uint8_t cogs_irq_off(void);

/* Pop n bytes of the pending DATA payload. Return 0 on success. */
uint8_t cogs_rx(uint8_t *dst, uint8_t n);

/* Send len bytes. Return 0 on success, -1 if the socket is gone. */
int cogs_send(const uint8_t *p, uint16_t len);

/* link_state 0 down, 1 joining, 2 up. ip is four octets, first octet first. */
int cogs_netinfo(uint8_t *link_state, uint32_t *ip);

uint8_t cogs_last_error(void);
const char *cogs_strerror(uint8_t code);

#endif
