#ifndef COPY_H
#define COPY_H

#include <stdint.h>

#define CP_OK     0
#define CP_ABORT  0xF0            /* user pressed ESC */
#define CP_SMB    0xF1            /* SMB error (see smb_status / smb_errstr) */
#define CP_EXISTS 0xF2            /* target already exists on the SMB share */
#define CP_BAS    0xF3            /* BASIC conversion failed (ROM table or program too large) */

/* Copy one local ProDOS file (contents, type, aux type, access).
   src/dst are full paths; dst must not exist. Returns 0, CP_ABORT or an MLI error.
   On error/abort the partial destination file is removed. */
uint8_t copy_local_file(const char *src, const char *dst, const char *label);

/* Download one SMB file to a new ProDOS file with the given type/aux type.
   spath is the share path ("/dir/file"), dst a ProDOS path that must not exist. */
uint8_t copy_smb_down(const char *spath, const char *dst, uint8_t type, uint16_t aux, const char *label);

/* Upload a local ProDOS file to the share (spath "/dir/NAME#TTAAAA"). Returns CP_EXISTS if the
   target exists and overwrite is 0. */
uint8_t copy_smb_up(const char *src, const char *spath, const char *label, uint8_t overwrite);

/* Remember the directory SMBGET.SYSTEM was started from (where SMBGET.OVL lives). */
void copy_set_home(const char *dir);

#endif
