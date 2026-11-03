/* ProDOS MLI wrappers: volumes, directories, files */
#ifndef PRODOS_H
#define PRODOS_H

#include <stdint.h>
#include "names.h"

#define PD_PATH_MAX 64              /* longest ProDOS path incl. leading '/' */

/* fentry_t.flags */
#define F_TAG 0x01
#define F_DIR 0x02
#define F_UP  0x04                  /* the ".." entry */

typedef struct {
    char     name[PD_NAME_MAX + 1];
    uint8_t  type;
    uint8_t  flags;
    uint16_t blocks;
    uint32_t size;                  /* EOF in bytes */
    uint16_t aux;
    uint16_t date;                  /* ProDOS date word */
    uint16_t hname;                 /* SMB: offset of the real host name in the pane's aux name heap */
} fentry_t;

typedef struct {
    char     name[PD_NAME_MAX + 1];
    uint8_t  unit;                  /* slot<<4 | drive<<7 */
    uint16_t total;
    uint16_t used;
} vol_t;

typedef struct {
    uint8_t  access;
    uint8_t  type;
    uint16_t aux;
    uint8_t  storage;
    uint16_t blocks;
    uint16_t date;
    uint16_t time;
} finfo_t;

#define PD_ERR_EOF      0x4C
#define PD_ERR_NOTFOUND 0x46
#define PD_ERR_DUP      0x47
#define PD_ERR_DISKFULL 0x48

uint8_t pd_online(vol_t *v, uint8_t max, uint8_t *count);
uint8_t pd_getprefix(char *out);                 /* "/VOL" or "/VOL/DIR", no trailing '/' */
/* Calls cb for each entry; cb returns nonzero to stop early. */
typedef uint8_t (*pd_dir_cb)(const fentry_t *f);
uint8_t pd_readdir(const char *path, pd_dir_cb cb);
uint8_t pd_getinfo(const char *path, finfo_t *fi);
uint8_t pd_setinfo(const char *path, const finfo_t *fi);
uint8_t pd_create(const char *path, uint8_t type, uint16_t aux);
uint8_t pd_mkdir(const char *path);
uint8_t pd_destroy(const char *path);
uint8_t pd_rename(const char *path, const char *newpath);

/* Open files. slot (0..2) picks a page-aligned 1K I/O buffer. */
uint8_t pd_open(const char *path, uint8_t slot, uint8_t *ref);
uint8_t pd_read(uint8_t ref, void *buf, uint16_t n, uint16_t *got);
uint8_t pd_write(uint8_t ref, const void *buf, uint16_t n);
uint8_t pd_close(uint8_t ref);

const char *pd_errstr(uint8_t err);

/* path helpers; path buffers must be PD_PATH_MAX+1 */
uint8_t pd_path_join(char *path, const char *name);   /* returns 0 if too long */
void    pd_path_parent(char *path);                   /* strips last component, "" at volume root */

#endif
