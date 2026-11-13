/* NIC overlay loaded into the hole at nic_hole. The first bytes are this
 * struct. cc65 does not pad it. tools/mknic.py patches load_size at offset 6.
 * Offsets: probe 8, init 10, connect 12, send 14, recv 16, close 18.
 */
#ifndef NIC_H
#define NIC_H

#include <stdint.h>

#define NIC_HOLE_SIZE 0x1800u

struct nic {
    char magic[4];                 /* 'N' 'I' 'C' '1' */
    uint8_t flags;
    uint8_t kind;
    uint16_t load_size;            /* file bytes, patched after the link */
    uint8_t (*probe)(uint8_t slot);
    uint8_t (*init)(uint8_t slot);
    uint8_t (*connect)(uint32_t addr, uint16_t port);
    int16_t (*send)(const uint8_t *p, uint16_t n);
    int16_t (*recv)(uint8_t *p, uint16_t max);
    void (*close)(void);
};

uint8_t nic_bringup(void);
uint8_t nic_tcp_connect(const uint8_t *ip, uint16_t port);
int16_t nic_tcp_send(const uint8_t *p, uint16_t n);
int16_t nic_tcp_recv(uint8_t *p, uint16_t max);
void    nic_tcp_close(void);

#endif
