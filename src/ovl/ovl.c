/* BASIC conversion overlay: glue between the file transfer loops in copy.c and bas.c. */
#include <stdint.h>
#include "ovl.h"
#include "bas.h"
#include "../copy.h"

static const ovl_api_t *A;
static uint16_t bas_ll, bas_written;

/* eth_outp is 352 bytes (src/eth_buffer.s). Download uses the start of it for one
   text line. Upload keeps a read-ahead chunk and one tokenized line (239 bytes plus
   the terminating 0) in it at the same time, so the chunk is whatever is left. */
#define ETH_OUT  352u
#define BAS_READ (ETH_OUT - (BAS_LINE_MAX + 1))
#define BAS_LINE ((char *)A->outp)
#define BAS_IN   (A->outp)
#define BAS_BODY (A->outp + BAS_READ)

/* ---- download: text -> tokenized program ------------------------------------------ */

static uint8_t line_done(uint8_t dref)
{
    uint8_t *o = A->pkt + 500;
    uint16_t n, link;
    uint8_t err;

    BAS_LINE[bas_ll] = 0;
    bas_ll = 0;
    n = bas_tok_line(BAS_LINE, o);
    if (!n) return 0;
    link = (uint16_t)(0x0801 + bas_written + n);
    o[0] = (uint8_t)link;
    o[1] = (uint8_t)(link >> 8);
    err = A->pd_write(dref, o, n);
    bas_written = (uint16_t)(bas_written + n);
    return err;
}

static uint8_t feed(uint8_t dref, const uint8_t *p, uint16_t n)
{
    uint8_t c, err;

    while (n--) {
        c = *p++;
        if (c == '\n' || c == '\r') {
            if (bas_ll && (err = line_done(dref)) != 0) return err;
        } else if (bas_ll < BAS_LINE_MAX) {
            BAS_LINE[bas_ll++] = (char)c;
        }
    }
    return 0;
}

static uint8_t finish(uint8_t dref)
{
    static const uint8_t zeros[2] = { 0, 0 };
    uint8_t err;

    if (bas_ll && (err = line_done(dref)) != 0) return err;
    return A->pd_write(dref, zeros, 2);
}

/* ---- upload: tokenized program -> text ----------------------------------------------- */

static uint16_t ipos, ilen;

static int16_t getb(uint8_t sref)
{
    if (ipos == ilen) {
        ipos = 0;
        ilen = 0;
        if (A->pd_read(sref, BAS_IN, BAS_READ, &ilen) || !ilen) return -1;
    }
    return BAS_IN[ipos++];
}

static uint8_t upload(uint8_t sref, uint32_t total)
{
    uint8_t *w = A->wbuf;
    uint16_t wn = 0, ln, tn;
    uint32_t sent = 0, rd = 0;
    int16_t b;
    uint8_t lo, hi;

    if (bas_init(A->inp)) return CP_BAS;
    ipos = ilen = 0;
    for (;;) {
        if (A->aborted()) return CP_ABORT;
        if ((b = getb(sref)) < 0) break;                  /* link, low byte */
        lo = (uint8_t)b;
        if ((b = getb(sref)) < 0) break;
        if (lo == 0 && b == 0) break;                     /* end of program */
        if ((b = getb(sref)) < 0) break;                  /* line number */
        lo = (uint8_t)b;
        if ((b = getb(sref)) < 0) break;
        hi = (uint8_t)b;
        for (tn = 0; (b = getb(sref)) > 0; ) {            /* tokens up to the 0 */
            if (tn >= BAS_LINE_MAX) return CP_BAS;
            BAS_BODY[tn++] = (uint8_t)b;
        }
        BAS_BODY[tn] = 0;
        rd += (uint32_t)tn + 5;
        if (wn > 260) {                                   /* keep room for a long line */
            if (A->smb_write(sent, wn)) return CP_SMB;
            sent += wn;
            wn = 0;
        }
        ln = bas_detok_line((uint16_t)(lo | (hi << 8)), BAS_BODY, (char *)w + wn, (uint16_t)(WBUF_MAX - wn - 1));
        if (ln == 0xFFFF) return CP_BAS;
        wn = (uint16_t)(wn + ln);
        w[wn++] = '\n';
        A->progress(rd, total);
    }
    if (wn && A->smb_write(sent, wn)) return CP_SMB;
    return 0;
}

/* ---- entry ----------------------------------------------------------------------------- */

uint8_t ovl_entry(uint8_t op, const ovl_api_t *api, uint8_t ref, const uint8_t *p, uint16_t n, uint32_t total)
{
    A = api;
    switch (op) {
    case OVL_DOWN_BEGIN:
        if (bas_init(A->inp)) return CP_BAS;
        bas_ll = bas_written = 0;
        return 0;
    case OVL_DOWN_FEED: return feed(ref, p, n);
    case OVL_DOWN_END:  return finish(ref);
    case OVL_UP:        return upload(ref, total);
    }
    return 0;
}
