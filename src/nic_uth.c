/* Uthernet II entry points for the SMBGET.UTH overlay.
 * The mode-register probe restores what it read. MegaFlash does not read
 * that register back, so the main program must not call uth_probe on it.
 */
#pragma optimize      (on)
#pragma static-locals (on)

#include <ip65.h>
#include <string.h>
#include "net.h"
#include "w5100.h"

void timer_init(void);
uint16_t timer_read(void);

uint8_t uth_probe(uint8_t slot)
{
    volatile uint8_t *mode;
    volatile uint8_t *addr_hi;
    volatile uint8_t *addr_lo;
    volatile uint8_t *data;
    uint8_t saved;
    uint8_t a;
    uint8_t b;

    if (slot < 1 || slot > 7) {
        return 0;
    }
    mode = (uint8_t *)((uint16_t)slot << 4 | 0xC084);
    addr_hi = mode + 1;
    addr_lo = mode + 2;
    data = mode + 3;
    saved = *mode;
    *mode = 0x03;
    *addr_hi = 0x00;
    *addr_lo = 0x17;
    (void)*addr_lo;
    a = *data;
    b = *data;
    *mode = saved;
    return (uint8_t)(a == 0x07 && b == 0xD0);
}

uint8_t uth_init(uint8_t slot)
{
    net.up = 0;
    timer_init();
    if (ip65_init(slot)) {
        net_log(ip65_strerror(ip65_error));
        return NET_NOCARD;
    }
    net_log("DHCP: discover...");
    if (dhcp_init()) {
        net_log(ip65_strerror(ip65_error));
        return NET_NODHCP;
    }
    memcpy(net.mac, cfg_mac, 6);
    memcpy(net.ip, &cfg_ip, 4);
    memcpy(net.mask, &cfg_netmask, 4);
    memcpy(net.gw, &cfg_gateway, 4);
    memcpy(net.dns, &cfg_dns, 4);
    w5100_init(slot);
    w5100_config();
    net.up = 1;
    return NET_OK;
}

uint8_t uth_connect(uint32_t addr, uint16_t port)
{
    return w5100_connect_addr(addr, port) ? NET_OK : NET_TIMEOUT;
}

int16_t uth_send(const uint8_t *p, uint16_t n)
{
    volatile uint8_t *data = w5100_data;
    uint16_t left = n;
    uint16_t t0 = timer_read();

    while (left) {
        uint16_t snd;
        uint16_t i;

        snd = w5100_send_request();
        if (!snd) {
            if (!w5100_connected() || (uint16_t)(timer_read() - t0) > 8000u) {
                return -1;
            }
            continue;
        }
        if (snd > left) {
            snd = left;
        }
        for (i = 0; i < snd; i++) {
            w5100_pace();
            *data = p[i];
        }
        p += snd;
        left = (uint16_t)(left - snd);
        w5100_send_commit(snd);
        t0 = timer_read();
    }
    return (int16_t)n;
}

int16_t uth_recv(uint8_t *p, uint16_t max)
{
    volatile uint8_t *data = w5100_data;
    uint16_t avail;
    uint16_t i;

    avail = w5100_receive_request();
    if (!avail) {
        return w5100_connected() ? 0 : -1;
    }
    if (avail > max) {
        avail = max;
    }
    for (i = 0; i < avail; i++) {
        w5100_pace();
        p[i] = *data;
    }
    w5100_receive_commit(avail);
    return (int16_t)avail;
}

void uth_close(void)
{
    w5100_disconnect();
}
