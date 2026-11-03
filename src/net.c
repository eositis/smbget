#include <string.h>
#include <stdbool.h>
#include <ip65.h>
#include "net.h"
#include "w5100.h"

netcfg_t net;

/* IP65 (software stack, W5100 in MACRAW) is used only for DHCP. Then w5100_config() switches the
   chip to one hardware TCP socket with 8K buffers, and IP65 is never called again. */

uint8_t net_start(net_log_fn log)
{
    uint8_t s, found = 0;
    char msg[32];

    net.up = 0;
    for (s = net.slot ? net.slot : 1; s < 8; ++s) {
        if (!ip65_init(s)) { found = s; break; }
        if (net.slot) break;
    }
    if (!found) return NET_NOCARD;
    net.slot = found;
    strcpy(msg, "Uthernet II found in slot ");
    msg[26] = (char)('0' + found);
    msg[27] = 0;
    log(msg);
    memcpy(net.mac, cfg_mac, 6);
    log("DHCP: discover...");
    if (dhcp_init()) return NET_NODHCP;
    memcpy(net.ip, &cfg_ip, 4);
    memcpy(net.mask, &cfg_netmask, 4);
    memcpy(net.gw, &cfg_gateway, 4);
    memcpy(net.dns, &cfg_dns, 4);
    w5100_init(found);
    w5100_config();
    net.up = 1;
    return NET_OK;
}

uint8_t net_tcp_connect(const uint8_t *ip, uint16_t port)
{
    uint32_t a;
    memcpy(&a, ip, 4);
    return w5100_connect_addr(a, port) ? NET_OK : NET_TIMEOUT;
}

int16_t net_tcp_send(const uint8_t *p, uint16_t n)
{
    volatile uint8_t *data = w5100_data;
    uint16_t left = n, snd, i;
    uint16_t t0 = timer_read();

    while (left) {
        snd = w5100_send_request();
        if (!snd) {
            if (!w5100_connected() || (uint16_t)(timer_read() - t0) > 8000u) return -1;
            continue;
        }
        if (snd > left) snd = left;
        for (i = 0; i < snd; ++i) *data = p[i];
        p += snd;
        left = (uint16_t)(left - snd);
        w5100_send_commit(snd);
        t0 = timer_read();
    }
    return (int16_t)n;
}

int16_t net_tcp_recv(uint8_t *p, uint16_t max)
{
    volatile uint8_t *data = w5100_data;
    uint16_t avail, i;

    avail = w5100_receive_request();
    if (!avail) return w5100_connected() ? 0 : -1;
    if (avail > max) avail = max;
    for (i = 0; i < avail; ++i) p[i] = *data;
    w5100_receive_commit(avail);
    return (int16_t)avail;
}

uint16_t net_ms(void)
{
    return timer_read();
}

void net_tcp_close(void)
{
    w5100_disconnect();
}

void net_hex(char *out, uint32_t v)
{
    uint8_t i;
    for (i = 0; i < 8; ++i) out[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15];
    out[8] = 0;
}

const char *net_errstr(uint8_t e)
{
    switch (e) {
    case NET_NOCARD:  return "No Uthernet II found";
    case NET_NODHCP:  return "No DHCP reply (cable/server?)";
    case NET_TIMEOUT: return "Connection timed out";
    }
    return "Network error";
}

void ip_str(const uint8_t *ip, char *out)
{
    uint8_t i, n = 0, v, d;
    for (i = 0; i < 4; ++i) {
        v = ip[i];
        d = (uint8_t)(v / 100);
        if (d) out[n++] = (char)('0' + d);
        v = (uint8_t)(v % 100);
        if (d || v >= 10) out[n++] = (char)('0' + v / 10);
        out[n++] = (char)('0' + v % 10);
        if (i < 3) out[n++] = '.';
    }
    out[n] = 0;
}

uint8_t ip_parse(const char *s, uint8_t *ip)
{
    uint8_t part = 0, digits = 0;
    uint16_t v = 0;

    for (;; ++s) {
        if (*s >= '0' && *s <= '9') {
            v = (uint16_t)(v * 10 + (*s - '0'));
            if (v > 255 || ++digits > 3) return 0;
        } else if (*s == '.' || *s == 0) {
            if (!digits) return 0;
            ip[part++] = (uint8_t)v;
            v = 0;
            digits = 0;
            if (*s == 0) break;
            if (part == 4) return 0;
        } else {
            return 0;
        }
    }
    return part == 4;
}
