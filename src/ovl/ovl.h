/* Interface between SMBGET.SYSTEM and the BASIC conversion overlay.
   The NIC driver occupies $0800, so this overlay lives above the main BSS. */
#ifndef OVL_H
#define OVL_H

#include <stdint.h>

#define OVL_ADDR   0xA930u
#define OVL_MAXLEN 0x0FB8u

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
    uint8_t  *inp, *outp;                                /* eth_inp 880 bytes, eth_outp 352 bytes */
} ovl_api_t;

#define WBUF_MAX 660u              /* SMB_WRITE_MAX: the write data starts at byte 116 of the 776-byte packet */

typedef uint8_t (*ovl_entry_fn)(uint8_t op, const ovl_api_t *api, uint8_t ref, const uint8_t *p,
                                uint16_t n, uint32_t total);

#endif
