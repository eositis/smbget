/* Folder copy. copy_tree runs from language-card bank 2; the buffers stay in main RAM. */
#ifndef TREE_H
#define TREE_H

#include <stdint.h>
#include "prodos.h"

#define TREE_MAX 8

extern char sp[], dp[], hn[];
extern uint8_t smb_src, smb_dst;
extern uint16_t cp_files, cp_skip;
extern char cp_who[];
extern uint16_t tree_ix[TREE_MAX];
extern uint8_t tree_sl[TREE_MAX], tree_dl[TREE_MAX];
extern uint16_t pick_n, pick_i;
extern fentry_t pick_ent;
extern uint8_t pick_hit, pick_err, pick_smb;

uint8_t copy_tree(void);

#endif
