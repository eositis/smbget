/* CoGS entry points for the SMBGET.COG overlay.
 * The card owns DHCP and TCP. Sends are SEND frames; receives are popped
 * with cogs_rx because a plain load of the DATA register is not the access
 * the card documents.
 */
#pragma optimize      (on)
#pragma static-locals (on)

#include <string.h>
#include "cogs.h"
#include "net.h"
#include "w5100.h"

uint16_t timer_jiffy(void);
void timer_init(void);

uint8_t cog_probe(uint8_t slot)
{
    return cogs_probe(slot);
}

uint8_t cog_init(uint8_t slot)
{
    uint8_t link = 0;
    uint16_t t0;
    uint16_t t1;
    uint32_t ip = 0;

    net.up = 0;
    timer_init();
    if (!slot || !cogs_probe(slot)) {
        net_log("CoGS card not found");
        return NET_NOCARD;
    }
    w5100_init(slot);
    net_log("Waiting for network...");
    t0 = timer_jiffy();
    for (;;) {
        if (cogs_netinfo(&link, &ip) == 0 && link == 2) {
            break;
        }
        if ((uint16_t)(timer_jiffy() - t0) > 20000u) {
            net_log(link == 1 ? "CoGS: still joining" : "CoGS: network is down");
            return NET_NODHCP;
        }
        t1 = timer_jiffy();
        while ((uint16_t)(timer_jiffy() - t1) < 250) {
        }
    }
    memcpy(net.ip, &ip, 4);
    net.up = 1;
    return NET_OK;
}

uint8_t cog_connect(uint32_t addr, uint16_t port)
{
    return w5100_connect_addr(addr, port) ? NET_OK : NET_TIMEOUT;
}

int16_t cog_send(const uint8_t *p, uint16_t n)
{
    if (cogs_send(p, n)) {
        return -1;
    }
    return (int16_t)n;
}

int16_t cog_recv(uint8_t *p, uint16_t max)
{
    uint16_t n;
    uint16_t got;

    n = w5100_receive_request();
    if (!n) {
        return w5100_connected() ? 0 : -1;
    }
    if (n > max) {
        n = max;
    }
    got = 0;
    while (got < n) {
        uint16_t left = (uint16_t)(n - got);
        uint8_t chunk = left > 255 ? 255 : (uint8_t)left;

        if (cogs_rx(p + got, chunk)) {
            return -1;
        }
        got = (uint16_t)(got + chunk);
    }
    return (int16_t)got;
}

void cog_close(void)
{
    w5100_disconnect();
}
