/* A file pane: either a local ProDOS directory or an SMB share directory */
#ifndef PANE_H
#define PANE_H

#include <stdint.h>
#include "prodos.h"

#define PK_LOCAL 0
#define PK_SMB   1

#define MAX_ENTRIES 200              /* per pane, stored in aux memory */
#define NAMEHEAP    10240u           /* per pane: real SMB host names, in aux memory */
#define NAME_NONE   0xFFFFu

typedef struct {
    uint8_t   kind;
    uint8_t   x;                         /* screen column of the pane (0 or 40) */
    char      path[PD_PATH_MAX + 1];
    uint16_t  auxbase;                   /* offset of this pane's entries in aux store */
    uint16_t  heapbase, heapused;        /* aux name heap */
    uint16_t  count, top, cur;
    uint16_t  tagged;
    uint16_t  free_blocks;
    uint8_t   truncated;                 /* listing hit MAX_ENTRIES */
    uint8_t   loaded;
} pane_t;

uint8_t pane_load(pane_t *p);            /* (re)read listing; returns MLI error */
void    pane_draw(const pane_t *p, uint8_t active);       /* full pane */
void    pane_draw_row(const pane_t *p, uint16_t idx, uint8_t active);
void    pane_move(pane_t *p, int16_t delta, uint8_t active);
void    pane_tag(pane_t *p);
void    pane_tag_all(pane_t *p);
void    pane_hostname(const pane_t *p, const fentry_t *f, char *out);   /* out >= 80 */
fentry_t *pane_cur(pane_t *p);           /* scratch copy, valid until next pane_cur */
void    pane_get(const pane_t *p, uint16_t idx, fentry_t *out);
void    pane_put(const pane_t *p, uint16_t idx, const fentry_t *f);
uint8_t aux_present(void);
void    aux_write(uint16_t off, const void *src, uint16_t len);
void    aux_read(uint16_t off, void *dst, uint16_t len);
void    aux_mainbank(void);

#endif
