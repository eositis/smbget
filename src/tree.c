/* Folder walk. Linked into the language card with the name tables.
   Routines that need ROM are reached through main-RAM stubs. */
#include <string.h>
#include "copy.h"
#include "names.h"
#include "tree.h"

extern uint8_t next_child(void);
extern uint8_t copy_file_paths(const char *label, uint8_t type, uint16_t aux, uint16_t date);
extern uint8_t join_share(char *path, const char *name);
extern uint8_t ensure_dir(void);

static uint8_t join_child(char *path, uint8_t saved, const char *name, uint8_t share)
{
    path[saved] = 0;
    if (share) return join_share(path, name);
    return pd_path_join(path, name);
}

uint8_t copy_tree(void)
{
    uint8_t depth = 0, err, n;
    const char *srcname, *destname;

    if (!smb_src && !smb_dst) {
        n = (uint8_t)strlen(sp);
        if (!strncmp(dp, sp, n) && (dp[n] == 0 || dp[n] == '/')) return CP_INTO;
    }
    err = ensure_dir();
    if (err) return err;
    tree_sl[0] = (uint8_t)strlen(sp);
    tree_dl[0] = (uint8_t)strlen(dp);
    tree_ix[0] = 0;
    for (;;) {
        pick_n = tree_ix[depth];
        pick_smb = smb_src;
        if (!next_child()) {
            if (pick_err) {
                if (smb_src) return CP_SMB;
                return pick_err;
            }
            if (depth == 0) return 0;
            --depth;
            sp[tree_sl[depth]] = 0;
            dp[tree_dl[depth]] = 0;
            continue;
        }
        ++tree_ix[depth];
        srcname = pick_ent.name;
        if (smb_src && hn[0]) srcname = hn;
        if (smb_dst) {
            names_prodos_to_host(pick_ent.name, pick_ent.type, pick_ent.aux, hn);
            destname = hn;
        } else {
            destname = pick_ent.name;
        }
        if (!join_child(sp, tree_sl[depth], srcname, smb_src)) return CP_LONG;
        if (pick_ent.flags & F_DIR) {
            if (depth + 1 >= TREE_MAX) return CP_DEEP;
            if (!join_child(dp, tree_dl[depth], destname, smb_dst)) return CP_LONG;
            err = ensure_dir();
            if (err) return err;
            ++depth;
            tree_sl[depth] = (uint8_t)strlen(sp);
            tree_dl[depth] = (uint8_t)strlen(dp);
            tree_ix[depth] = 0;
            continue;
        }
        if (!join_child(dp, tree_dl[depth], destname, smb_dst)) return CP_LONG;
        err = copy_file_paths(pick_ent.name, pick_ent.type, pick_ent.aux, pick_ent.date);
        sp[tree_sl[depth]] = 0;
        dp[tree_dl[depth]] = 0;
        if (err == CP_EXISTS) ++cp_skip;
        else if (err) {
            memcpy(cp_who, pick_ent.name, PD_NAME_MAX);
            cp_who[PD_NAME_MAX] = 0;
            return err;
        } else {
            ++cp_files;
        }
    }
}
