/* ProDOS <-> host file name mapping (portable C; also built on the host for tests) */
#ifndef NAMES_H
#define NAMES_H

#include <stdint.h>

#define PD_NAME_MAX 15              /* ProDOS file name length */

#define PT_FOT 0x08
#define PT_TXT 0x04
#define PT_BIN 0x06
#define PT_DIR 0x0F
#define PT_BAS 0xFC
#define PT_SYS 0xFF

/* Convert a host (SMB) name to a ProDOS name and file type/aux type.
   pname must hold PD_NAME_MAX+1 bytes. A trailing #TTAAAA (6 hex digits)
   sets type/aux and is removed; otherwise the extension picks a type.
   Returns 1 if the host name carried an explicit #TTAAAA suffix. */
uint8_t names_host_to_prodos(const char *host, char *pname, uint8_t *type, uint16_t *aux);

/* Build the host name NAME#TTAAAA for a ProDOS file. host must hold
   PD_NAME_MAX+8 bytes. Directories get no suffix. */
void names_prodos_to_host(const char *pname, uint8_t type, uint16_t aux, char *host);

/* Three-letter display for a ProDOS file type; out holds 4 bytes ("$xx" if unknown). */
void names_type_str(uint8_t type, char *out);

#endif
