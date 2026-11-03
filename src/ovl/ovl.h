/* Interface between SMBGET.SYSTEM and the BASIC conversion overlay (SMBGET.OVL, loaded at $0800).
   The overlay has no link-time ties to the main program: everything it needs is passed in. */
#ifndef OVL_H
#define OVL_H

#include <stdint.h>

#define OVL_ADDR   0x0800u
#define OVL_MAXLEN 0x1400u            /* code + data; its BSS sits at $1C00-$1FFF */

#define OVL_DOWN_BEGIN 1              /* text -> tokenized program, one chunk at a time */
#define OVL_DOWN_FEED  2
#define OVL_DOWN_END   3
#define OVL_UP         4              /* tokenized program (open ProDOS file) -> text via smb_write */

typedef struct {
    uint8_t  (*pd_read)(uint8_t ref, void *buf, uint16_t n, uint16_t *got);
    uint8_t  (*pd_write)(uint8_t ref, const void *buf, uint16_t n);
    uint8_t  (*smb_write)(uint32_t off, uint16_t len);   /* sends len bytes stored at wbuf */
    void     (*progress)(uint32_t done, uint32_t total);
    uint8_t  (*aborted)(void);                           /* 1 if the user pressed ESC */
    uint8_t  *wbuf;                                      /* SMB write data area, WBUF_MAX bytes */
    uint8_t  *pkt;                                       /* SMB packet buffer (output line at +500) */
    uint8_t  *inp, *outp;                                /* 880 + 400 bytes of idle scratch memory */
} ovl_api_t;

#define WBUF_MAX 680u

typedef uint8_t (*ovl_entry_fn)(uint8_t op, const ovl_api_t *api, uint8_t ref, const uint8_t *p,
                                uint16_t n, uint32_t total);

#endif
