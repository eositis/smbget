/* Network layer. Card drivers live in SMBGET.UTH and SMBGET.COG. */
#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_OK        0
#define NET_NOCARD    1
#define NET_NODHCP    2
#define NET_TIMEOUT   3
#define NET_REFUSED   4
#define NET_CLOSED    5

typedef struct {
    uint8_t slot;                   /* 0 = auto-scan */
    uint8_t up;                     /* IP configured */
    uint8_t mac[6];
    uint8_t ip[4], mask[4], gw[4], dns[4];
} netcfg_t;

extern netcfg_t net;

typedef void (*net_log_fn)(const char *msg);

void    net_log(const char *msg);
uint8_t net_start(net_log_fn log);          /* find card, bring up its driver */
uint8_t net_tcp_connect(const uint8_t *ip, uint16_t port);
int16_t net_tcp_send(const uint8_t *p, uint16_t n);     /* n sent, or -1 */
int16_t net_tcp_recv(uint8_t *p, uint16_t max);         /* bytes, 0 = nothing yet, -1 = closed */
void    net_tcp_close(void);
uint16_t net_ms(void);                      /* millisecond clock (wraps at 65536) */

void    net_hex(char *out, uint32_t v);             /* 8 hex digits + NUL */
const char *net_errstr(uint8_t e);
void    ip_str(const uint8_t *ip, char *out);       /* out >= 16 bytes */
uint8_t ip_parse(const char *s, uint8_t *ip);       /* 1 if valid dotted quad */

#endif
