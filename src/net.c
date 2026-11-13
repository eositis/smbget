#include <string.h>
#include "net.h"
#include "nic.h"

netcfg_t net;

static net_log_fn logfn;

uint16_t timer_read(void);

void net_log(const char *msg)
{
    if (logfn) {
        logfn(msg);
    }
}

uint8_t net_start(net_log_fn log)
{
    logfn = log;
    return nic_bringup();
}

uint16_t net_ms(void)
{
    return timer_read();
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
    case NET_NOCARD:  return "No network card";
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
