/* Host harness: runs src/smb2.c against a real server over BSD sockets and dumps what it sends. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include "../../src/smb2.h"

netcfg_t net;
uint8_t smbpkt[800];
static int fd = -1;
static void hexdump(const uint8_t *p, uint16_t n) { for (uint16_t i = 0; i < n; ++i) printf("%02X%s", p[i], (i & 31) == 31 ? "\n" : " "); printf("\n"); }
uint8_t net_tcp_connect(const uint8_t *ip, uint16_t port)
{
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
    memcpy(&a.sin_addr, ip, 4);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    return connect(fd, (struct sockaddr *)&a, sizeof a) ? NET_TIMEOUT : NET_OK;
}
int16_t net_tcp_send(const uint8_t *p, uint16_t n) { printf("SEND %u:\n", n); hexdump(p, n); return write(fd, p, n) == n ? n : -1; }
int16_t net_tcp_recv(uint8_t *p, uint16_t max)
{
    fd_set s; struct timeval tv = {0, 200000};
    FD_ZERO(&s); FD_SET(fd, &s);
    if (select(fd + 1, &s, 0, 0, &tv) <= 0) return 0;
    int n = read(fd, p, max);
    if (n > 0) { printf("RECV %d\n", n); }
    return n > 0 ? n : -1;
}
void net_tcp_close(void) { if (fd >= 0) close(fd); fd = -1; }
uint16_t net_ms(void) { return (uint16_t)(clock() * 1000 / CLOCKS_PER_SEC); }
void ip_str(const uint8_t *ip, char *out) { sprintf(out, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]); }
static void lg(const char *m) { printf("== %s\n", m); }
static uint8_t cb(const fentry_t *f)
{
    printf("%-16s %s type %02X %8lu bytes  date %04X (%02u-%02u-%02u)\n", f->name, (f->flags & F_DIR) ? "DIR" : "   ", f->type, (unsigned long)f->size, f->date, (f->date >> 5) & 15, f->date & 31, f->date >> 9);
    return 0;
}

static uint8_t cb2(const fentry_t *f) { return 0; }

int main(int argc, char **argv)
{
    uint8_t ip[4]; unsigned a, b, c, d;
    sscanf(argv[1], "%u.%u.%u.%u", &a, &b, &c, &d);
    ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
    uint8_t r = smb_connect(ip, argc > 2 ? argv[2] : "public", lg);
    if (!r) {
        extern uint8_t smb_readdir(const char *, pd_dir_cb);
        r = smb_readdir(argc > 3 ? argv[3] : "/", cb);
        if (getenv("REPEAT")) {                       /* many requests in one session: message ids must not wrap */
            int k, n = atoi(getenv("REPEAT")), bad = 0;
            for (k = 0; k < n; ++k) if (smb_readdir("/", cb2)) ++bad;
            printf("REPEAT %d listings, %d failed\n", n, bad);
        }
        printf("readdir -> %u status %08X\n", r, (unsigned)smb_status);
    }
    if (!r && argc > 4) {
        uint32_t size; uint16_t got; uint32_t off = 0, sum = 0; const uint8_t *d;
        r = smb_open_file(argv[4], &size);
        printf("open %s -> %u size %lu status %08X\n", argv[4], r, (unsigned long)size, (unsigned)smb_status);
        while (!r && (d = smb_read(off, SMB_READ_MAX, &got)) && got) { for (uint16_t i = 0; i < got; ++i) sum += d[i]; if (off == 0) { printf("first bytes: %.*s\n", got > 40 ? 40 : got, d); } off += got; }
        printf("read %lu bytes, byte-sum %u\n", (unsigned long)off, (unsigned)sum);
        smb_close_file();
    }
    printf("result %u (%s) status %08X stage %u\n", r, smb_errstr(r), (unsigned)smb_status, smb_stage);
    return 0;
}
