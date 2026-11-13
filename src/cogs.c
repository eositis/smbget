/*
 * CoGS network transport for A2CHAT (build with -DA2CHAT_COGS).
 *
 * Implements the w5100_* API from w5100.h on top of the CoGS framed
 * command protocol, so http65.c / net.c keep their structure.
 *
 * Protocol summary (CoGS Technical Reference, ch. 3-5):
 *   frame      = OPCODE(1) LEN_LO(1) LEN_HI(1) PAYLOAD(LEN)
 *   CONNECT $10  sock, flags, port_lo, port_hi, host_len, host
 *   SEND    $11  sock, data (<= 2047)
 *   CLOSE   $12  sock
 *   NETINFO $08  -> $88  link_state, ip[4], ...
 *   LOCALTIME $0F -> $8F valid, auto, secs since 1904 (4, local time)
 *   events: CONNECTED $90 (sock,result)  DATA $91 (sock,data...)
 *           CLOSED $92 (sock,reason)      ERROR $93 (sock,code)
 *           SEND_ACK $9A (sock,result)
 *
 * Card->host frames are never interleaved, so the receive side is a
 * single reader: poll_one() handles one whole frame, except that for a
 * DATA frame on our socket it consumes only the header and socket byte
 * and leaves the payload in the FIFO (rx_left). The HTTP code then pops
 * the payload straight from the DATA register, with no copy and no
 * 1 KB staging buffer. No other frame is read while rx_left != 0.
 */

#pragma static-locals (on)
#pragma optimize      (on)

#include <conio.h>
#include <string.h>
#include "net.h"
#include "w5100.h"
#include "cogs.h"

#define SOCK        0
#define COGS_FIFO_SIZE 16384u   /* card-to-host FIFO (reference 3.3) */
#define SEND_CHUNK  1024

#define OP_NETINFO  0x08
#define OP_LOCALTIME 0x0F
#define OP_CONNECT  0x10
#define OP_SEND     0x11
#define OP_CLOSE    0x12

#define EV_CONNECTED 0x90
#define EV_DATA      0x91
#define EV_CLOSED    0x92
#define EV_ERROR     0x93
#define EV_SEND_ACK  0x9A

#define REG_STATUS   1
#define ST_RX_AVAIL  0x80

#define S_CLOSED     0
#define S_CONNECTING 1
#define S_OPEN       2
#define S_CLOSING    3

/* timer_jiffy() counts VBL edges, 16 per frame. */
uint16_t timer_jiffy(void);

static uint8_t input_check_for_abort_key(void)
{
    return (uint8_t)(kbhit() && cgetc() == CH_ESC);
}

volatile uint8_t *w5100_data;

static uint8_t  st;
static uint16_t rx_left;     /* our DATA payload bytes still in the FIFO */
static uint8_t  tx_unacked;  /* SEND frames not yet acknowledged       */
static uint8_t  last_err;

static uint8_t  want_op;     /* reply opcode a caller is waiting for   */
static uint8_t  want_got;
static uint8_t  want_buf[8];

static uint8_t  hdr[5];

/* Trouble seen since the last connect, for cogs_report(). */
static uint8_t  n_err;       /* ERROR events for our socket */
static uint8_t  n_resync;    /* times the FIFOs were reset */
static uint16_t n_drop;      /* our DATA payload bytes thrown away */
static uint8_t  n_bad;       /* frames with an opcode a card never sends */
static uint8_t  n_irq;       /* times we found IRQs enabled */
static uint32_t n_rx;        /* our DATA payload bytes read */
static uint16_t q_peak;      /* most card->host bytes queued at once */
static uint16_t t_open;      /* timer_jiffy() when the socket opened */
static uint16_t t_last;      /* timer_jiffy() at the last payload read */

/* Disk I/O between requests can leave IRQs on. Keep them off while we
 * own the FIFO, and count how often they were found on. */
static void irq_off(void)
{
    if (cogs_irq_off() && n_irq < 255) {
        ++n_irq;
    }
}

const char *cogs_strerror(uint8_t code)
{
    switch (code) {
    case 1:  return "no network";
    case 2:  return "DNS failed";
    case 3:  return "timed out";
    case 4:  return "refused";
    case 5:  return "TLS failed";
    case 7:  return "no free socket";
    case 8:  return "socket state";
    case 9:  return "overrun";
    case 10: return "bad frame";
    case 15: return "card busy";
    }
    return "card error";
}

uint8_t cogs_last_error(void)
{
    return last_err;
}

/* Transport lost frame alignment (a byte never arrived). Reset the card's
 * parser and both FIFOs; anything in flight on our socket is gone. */
static void desync(void)
{
    ++n_resync;
    cogs_resync();
    rx_left = 0;
    tx_unacked = 0;
    st = S_CLOSED;
    last_err = 10;
}

static uint8_t send_frame(uint8_t op, const uint8_t *pre, uint8_t npre,
                          const uint8_t *body, uint16_t nbody)
{
    uint16_t len = (uint16_t)(npre + nbody);

    hdr[0] = op;
    hdr[1] = (uint8_t)len;
    hdr[2] = (uint8_t)(len >> 8);
    if (cogs_putn(hdr, 3)) {
        return 1;
    }
    if (npre && cogs_putn(pre, npre)) {
        return 1;
    }
    if (nbody && cogs_putn(body, nbody)) {
        return 1;
    }
    return 0;
}

/* Read up to two leading payload bytes of a frame into hdr[3..4] and skip
 * the rest. Return nonzero on timeout. */
static uint8_t read_small(uint16_t len, uint8_t *dst, uint8_t cap)
{
    int b;
    uint8_t i = 0;

    while (len && i < cap) {
        b = cogs_get();
        if (b < 0) {
            return 1;
        }
        dst[i++] = (uint8_t)b;
        --len;
    }
    return len ? cogs_skip(len) : 0;
}

/*
 * Handle at most one card->host frame.
 *   0  nothing queued
 *   1  handled a frame (not our data)
 *   2  DATA for our socket is pending (rx_left > 0)
 *  -1  transport error (card resynchronized)
 */
static int8_t poll_one(void)
{
    int b;
    uint8_t op, sock;
    uint16_t len;
    uint8_t *pl = hdr + 3;

    if (rx_left) {
        return 2;
    }
    if (!(cogs_reg_rd(REG_STATUS) & ST_RX_AVAIL)) {
        return 0;
    }

    if ((b = cogs_get()) < 0) goto bad;
    op = (uint8_t)b;
    if ((b = cogs_get()) < 0) goto bad;
    len = (uint8_t)b;
    if ((b = cogs_get()) < 0) goto bad;
    len |= (uint16_t)b << 8;

    if (op == EV_DATA) {
        if (!len) {
            return 1;
        }
        if ((b = cogs_get()) < 0) goto bad;
        sock = (uint8_t)b;
        --len;
        if (sock == SOCK && st == S_OPEN && len) {
            rx_left = len;
            return 2;
        }
        /* Another socket, or ours while closing: drop it. */
        if (sock == SOCK && st != S_CLOSING) {
            n_drop += len;
        }
        if (len && cogs_skip(len)) goto bad;
        return 1;
    }

    if (want_op && op == want_op) {
        if (read_small(len, want_buf, sizeof want_buf)) goto bad;
        want_got = 1;
        return 1;
    }

    pl[0] = 0xFF;
    pl[1] = 0;
    if (read_small(len, pl, 2)) goto bad;
    sock = pl[0];

    switch (op) {
    case EV_CONNECTED:
        if (sock == SOCK && st == S_CONNECTING) {
            if (pl[1]) {
                last_err = pl[1];
                st = S_CLOSED;
            } else {
                st = S_OPEN;
            }
        }
        break;
    case EV_CLOSED:
        if (sock == SOCK) {
            st = S_CLOSED;
            tx_unacked = 0;
        }
        break;
    case EV_SEND_ACK:
        if (sock == SOCK) {
            if (tx_unacked) {
                --tx_unacked;
            }
            if (pl[1]) {
                last_err = pl[1];
            }
        }
        break;
    case EV_ERROR:
        if (sock == SOCK || sock == 0xFF) {
            ++n_err;
            last_err = pl[1];
            if (st == S_CONNECTING || sock == 0xFF) {
                st = S_CLOSED;
            }
            if (sock == 0xFF) {
                /* The card could not parse something we sent. */
                ++n_resync;
                cogs_resync();
                tx_unacked = 0;
            }
        }
        break;
    default:
        /* WIFI_STATE, unrelated replies, ... Card->host opcodes all have
         * bit 7 set, so anything else means we lost frame alignment. */
        if (!(op & 0x80)) {
            ++n_bad;
        }
        break;
    }
    return 1;

bad:
    desync();
    return -1;
}

/* Run poll_one() until the FIFO is empty or our data is pending. */
static void poll_all(void)
{
    uint8_t guard = 32;

    while (guard-- && poll_one() == 1) {
    }
}

/* Send a request and wait (bounded, abortable) for its reply opcode.
 * want_buf holds the first payload bytes. Return 0 if it arrived. */
static int request(uint8_t op, uint16_t ms)
{
    uint16_t t0;

    want_op = (uint8_t)(op | 0x80);
    want_got = 0;
    if (send_frame(op, 0, 0, 0, 0)) {
        want_op = 0;
        return -1;
    }
    t0 = timer_jiffy();
    while (!want_got) {
        if (poll_one() == 2) {
            /* Unexpected: our socket has data queued ahead of the
             * reply. Drop it so the reply can be reached. */
            cogs_skip(rx_left);
            rx_left = 0;
        }
        if (input_check_for_abort_key() ||
            (uint16_t)(timer_jiffy() - t0) > ms) {
            break;
        }
    }
    want_op = 0;
    return want_got ? 0 : -1;
}

int cogs_netinfo(uint8_t *link_state, uint32_t *ip)
{
    if (request(OP_NETINFO, 3000)) {
        return -1;
    }
    *link_state = want_buf[0];
    memcpy(ip, want_buf + 1, 4);
    return 0;
}

int cogs_localtime(uint32_t *secs)
{
    /* request() would throw away socket data queued ahead of the reply. */
    if (st != S_CLOSED || request(OP_LOCALTIME, 500) || !want_buf[0]) {
        return -1;
    }
    memcpy(secs, want_buf + 2, 4);
    return 0;
}

bool w5100_init(uint8_t eth_init)
{
    uint16_t t0;

    cogs_set_slot(eth_init);
    w5100_data = (volatile uint8_t *)(0xC080 + ((uint16_t)eth_init << 4));
    st = S_CLOSED;
    rx_left = 0;
    tx_unacked = 0;
    last_err = 0;
    want_op = 0;

    /* Start from a clean parser and empty FIFOs. (No soft reset: that
     * would also abort a network-disk transfer ProDOS might own.) */
    cogs_resync();

    /* A previous run may have left our socket open. Close it and let the
     * reply (CLOSED, or ERROR if it was not open) drain away. */
    {
        uint8_t s = SOCK;
        send_frame(OP_CLOSE, &s, 1, 0, 0);
    }
    t0 = timer_jiffy();
    while ((uint16_t)(timer_jiffy() - t0) < 300) {
        if (poll_one() == 2) {
            cogs_skip(rx_left);
            rx_left = 0;
        }
    }
    st = S_CLOSED;

    /* The card always resolves host names itself. */
    return true;
}

void w5100_config(void)
{
    /* Addressing, DNS and TCP live on the card. Nothing to do. */
}

bool w5100_connect_name(const char *name, uint8_t length, uint16_t port)
{
    uint8_t pre[5];
    uint16_t t0;
    uint8_t irq_was_on = cogs_irq_off();

    if (st != S_CLOSED) {
        w5100_disconnect();
    }
    pre[0] = SOCK;
    pre[1] = 0;                 /* plain TCP, no TLS */
    pre[2] = (uint8_t)port;
    pre[3] = (uint8_t)(port >> 8);
    pre[4] = length;
    last_err = 0;
    n_err = 0;
    n_resync = 0;
    n_drop = 0;
    n_bad = 0;
    n_irq = irq_was_on;
    n_rx = 0;
    q_peak = 0;
    rx_left = 0;
    tx_unacked = 0;
    st = S_CONNECTING;
    if (send_frame(OP_CONNECT, pre, 5, (const uint8_t *)name, length)) {
        desync();
        return false;
    }

    /* The card answers every CONNECT with CONNECTED or ERROR; its own
     * timeout covers dead hosts. This bound only guards a lost reply. */
    t0 = timer_jiffy();
    while (st == S_CONNECTING) {
        poll_one();
        if (st == S_OPEN) {
            t_open = t_last = timer_jiffy();
        }
        if (input_check_for_abort_key()) {
            w5100_disconnect();
            return false;
        }
        if ((uint16_t)(timer_jiffy() - t0) > 45000u) {
            last_err = 3;
            w5100_disconnect();
            return false;
        }
    }
    return st == S_OPEN;
}

bool w5100_connect_addr(uint32_t addr, uint16_t port)
{
    char ip[16];

    ip_str((const uint8_t *)&addr, ip);
    return w5100_connect_name(ip, (uint8_t)strlen(ip), port);
}

bool w5100_connected(void)
{
    if (rx_left) {
        return true;            /* data still to read counts as open */
    }
    poll_all();
    return st == S_OPEN || rx_left != 0;
}

void w5100_disconnect(void)
{
    uint16_t t0;
    uint8_t s = SOCK;

    if (rx_left) {
        if (cogs_skip(rx_left)) {
            desync();
        }
        rx_left = 0;
    }
    if (st == S_OPEN || st == S_CONNECTING) {
        st = S_CLOSING;
        if (send_frame(OP_CLOSE, &s, 1, 0, 0)) {
            desync();
            return;
        }
        t0 = timer_jiffy();
        while (st == S_CLOSING) {
            poll_one();         /* DATA for us is dropped while closing */
            if ((uint16_t)(timer_jiffy() - t0) > 3000) {
                break;
            }
        }
    }
    st = S_CLOSED;
    tx_unacked = 0;
}

uint16_t w5100_data_request(bool do_send)
{
    uint16_t a;

    if (do_send) {
        return 0;               /* use cogs_send() */
    }
    irq_off();
    if (!rx_left) {
        poll_all();
        if (!rx_left) {
            return 0;
        }
    }
    a = cogs_avail();
    if (a > q_peak) {
        q_peak = a;
    }
    return a < rx_left ? a : rx_left;
}

uint8_t cogs_rx(uint8_t *dst, uint8_t n)
{
    if (cogs_getn(dst, n)) {
        desync();
        return 1;
    }
    rx_left = (uint16_t)(rx_left - n);
    n_rx += n;
    t_last = timer_jiffy();
    return 0;
}

void w5100_data_commit(bool do_send, uint16_t size)
{
    if (!do_send) {
        rx_left = (uint16_t)(rx_left - size);
    }
}

void cogs_report(void)
{
}

int cogs_send(const uint8_t *p, uint16_t len)
{
    uint8_t s = SOCK;
    uint16_t n;
    uint16_t t0;

    irq_off();
    while (len) {
        /* One SEND in flight at a time. If the server is already
         * answering (our data pending), stop waiting for the ack. */
        t0 = timer_jiffy();
        while (tx_unacked && st == S_OPEN && !rx_left) {
            poll_one();
            if (input_check_for_abort_key()) {
                w5100_disconnect();
                return -1;
            }
            if ((uint16_t)(timer_jiffy() - t0) > 20000u) {
                last_err = 3;
                w5100_disconnect();
                return -1;
            }
        }
        if (st != S_OPEN || last_err) {
            return -1;
        }
        n = len > SEND_CHUNK ? SEND_CHUNK : len;
        if (send_frame(OP_SEND, &s, 1, p, n)) {
            desync();
            return -1;
        }
        ++tx_unacked;
        p += n;
        len = (uint16_t)(len - n);
    }
    return 0;
}
