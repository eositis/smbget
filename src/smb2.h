/* Minimal SMB2 client (dialect 2.0.2, anonymous/guest NTLMSSP) */
#ifndef SMB2_H
#define SMB2_H

#include <stdint.h>
#include "net.h"
#include "prodos.h"

#define SMB_OK        0
#define SMB_E_TCP     1
#define SMB_E_NEG     2     /* negotiate failed / unsupported */
#define SMB_E_SESSION 3     /* session setup refused (see smb_status) */
#define SMB_E_TREE    4     /* tree connect refused (see smb_status) */
#define SMB_E_PROTO   5     /* malformed or missing reply */
/* The packet is 776 bytes. A read reply is 4 bytes of NetBIOS length, then
   DataOffset (0x50) bytes from the SMB2 header, then the data: 684 leaves
   8 bytes when a server places the data slightly later than 0x50.
   A write's data starts at packet offset 116, so 660 fills the packet. */
#define SMB_READ_MAX  684u
#define SMB_WRITE_MAX 660u
#define SMB_E_STATUS  6     /* server returned an error (see smb_status) */

extern uint8_t  smb_why;
extern uint8_t  smb_stage;
extern uint32_t smb_status;     /* last NT status from the server */

/* connect, negotiate, guest session, tree connect \\ip\share */
uint8_t smb_connect(const uint8_t *ip, const char *share, net_log_fn log);
void    smb_disconnect(void);
uint8_t smb_connected(void);

/* List a share directory (path like "/" or "/games"); cb gets one entry each, nonzero stops. */
extern const char *dir_hname;     /* real host name of the entry being passed to the callback */
uint8_t smb_readdir(const char *path, pd_dir_cb cb);
uint8_t smb_open_file(const char *path, uint32_t *size);
void    smb_close_file(void);
uint8_t smb_create_file(const char *path, uint8_t overwrite);
uint8_t smb_mkdir(const char *path);
uint8_t smb_remove(const char *path, uint8_t directory);
uint8_t smb_rename(const char *path, const char *newpath);
uint8_t *smb_write_buf(void);
void    smb_set_normal(void);           /* clear the archive bit of the open file */
uint8_t smb_write(uint32_t off, uint16_t len);   /* sends the bytes stored at smb_write_buf() */
uint8_t *smb_scratch(uint16_t *size);   /* the packet buffer, free to use between SMB calls */
const uint8_t *smb_read(uint32_t off, uint16_t want, uint16_t *got);   /* NULL on error */
const char *smb_errstr(uint8_t e);

#endif
