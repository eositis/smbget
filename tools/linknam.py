#!/usr/bin/env python3
"""Link names.c into a language-card image that loads at $D400.

The three C functions stay in this image. Everything else they call
(memcpy, strlen, strcpy, the zero page) is imported from the main
program so the image does not carry a second copy. memchr is pulled
from the library when the main program does not have it.

Usage: linknam.py LABEL OUT MAP CFG -- nam_vec.o names.o apple2enh.lib
"""
import os
import re
import subprocess
import sys

START = 0xD400
SIZE = 0x0C00
KEEP = {
    "_names_host_to_prodos",
    "_names_prodos_to_host",
    "_names_type_str",
    "_copy_tree",
}
# Calls that need ROM are imported at the main-RAM stub, not the real routine.
STUBS = {
    "_ensure_dir": "_lcstub_ensure_dir",
    "_next_child": "_lcstub_next_child",
    "_copy_file_paths": "_lcstub_copy_file_paths",
}
FORCE = [
    "sp", "sreg", "regsave", "regbank",
    "ptr1", "ptr2", "ptr3", "ptr4",
    "tmp1", "tmp2", "tmp3", "tmp4",
]


def load_lbl(path):
    labs = {}
    for line in open(path):
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "al":
            name = parts[2][1:] if parts[2].startswith(".") else parts[2]
            labs[name] = int(parts[1], 16)
    return labs


def write_cfg(path, imports):
    lines = [
        "MEMORY {",
        "    LC: start = $%04X, size = $%04X, file = %%O;" % (START, SIZE),
        "}",
        "SEGMENTS {",
        "    CODE:   load = LC, type = ro;",
        "    RODATA: load = LC, type = ro;",
        "    DATA:   load = LC, type = rw;",
        "    BSS:    load = LC, type = bss, define = yes;",
        "}",
        "SYMBOLS {",
    ]
    for name in sorted(imports):
        if imports[name] < 0x100:
            lines.append("    %s: type = export, value = $%04X, addrsize = zeropage;"
                         % (name, imports[name]))
        else:
            lines.append("    %s: type = export, value = $%04X;"
                         % (name, imports[name]))
    lines.append("}")
    lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def parse_problems(text):
    unresolved = set(re.findall(r"Unresolved external '([^']+)'", text))
    unresolved |= set(re.findall(r'Unresolved external "([^"]+)"', text))
    dups = set(re.findall(r"Duplicate (?:external )?(?:symbol|identifier):? '([^']+)'", text))
    dups |= set(re.findall(r'Duplicate (?:external )?(?:symbol|identifier):? "([^"]+)"', text))
    return unresolved, dups


def main():
    if "--" not in sys.argv:
        sys.stderr.write("usage: linknam.py LABEL OUT MAP CFG -- objs...\n")
        return 1
    sep = sys.argv.index("--")
    label, out_path, map_path, cfg_path = sys.argv[1:sep]
    objs = sys.argv[sep + 1:]
    libs = [o for o in objs if o.endswith(".lib") or o.endswith(".a")]
    objs = [o for o in objs if o not in libs]
    main_labs = load_lbl(label)
    imports = {}
    for name in FORCE:
        if name in main_labs:
            imports[name] = main_labs[name]
    banned = set(KEEP)
    lbl_path = out_path + ".lbl"
    use_libs = False

    for attempt in range(12):
        write_cfg(cfg_path, imports)
        cmd = ["ld65", "-C", cfg_path, "-o", out_path, "-m", map_path,
               "-Ln", lbl_path] + objs
        if use_libs:
            cmd += libs
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              universal_newlines=True)
        text = proc.stdout or ""
        if proc.returncode == 0:
            over_labs = load_lbl(lbl_path)
            extra = {}
            for name, addr in over_labs.items():
                if name.startswith("__") or name in imports or name in banned:
                    continue
                if name in main_labs and addr >= START:
                    extra[name] = main_labs[name]
            if not extra:
                break
            imports.update(extra)
            continue
        unresolved, dups = parse_problems(text)
        changed = False
        for name in list(dups):
            if name in imports and name not in KEEP:
                del imports[name]
                banned.add(name)
                changed = True
        missing = False
        for name in unresolved:
            if name in KEEP:
                missing = True
                continue
            target = STUBS.get(name, name)
            if target in main_labs and name not in imports and name not in banned:
                imports[name] = main_labs[target]
                changed = True
            elif name not in main_labs:
                missing = True
        if missing and not use_libs:
            use_libs = True
            changed = True
        if not changed:
            sys.stderr.write(text)
            sys.stderr.write("linknam: link failed\n")
            return 1
    else:
        sys.stderr.write("linknam: too many passes\n")
        return 1

    used = START
    bss = 0
    for line in open(map_path):
        parts = line.split()
        if len(parts) >= 4 and parts[0] in ("CODE", "RODATA", "DATA", "BSS"):
            try:
                start = int(parts[1], 16)
                size = int(parts[3], 16)
            except ValueError:
                continue
            if parts[0] == "BSS":
                bss = size
            elif start + size > used:
                used = start + size
    if bss:
        sys.stderr.write("linknam: image has %u bytes of BSS\n" % bss)
        return 1
    if used - START > SIZE:
        sys.stderr.write("linknam: image needs %u bytes, bank is %u\n" % (used - START, SIZE))
        return 1
    data = open(out_path, "rb").read()
    if len(data) < 6 or not (0xD4 <= data[1] < 0xE0):
        sys.stderr.write("linknam: vector table is not at $D400\n")
        return 1
    sys.stdout.write("SMBGET.NAM %u bytes at $%04X\n" % (len(data), START))
    return 0


if __name__ == "__main__":
    sys.exit(main())
