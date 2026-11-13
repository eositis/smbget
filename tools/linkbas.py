#!/usr/bin/env python3
"""Link SMBGET.OVL at a fixed address, importing routines the main program already has.

Usage: linkbas.py LABEL OUT MAP CFG -- objs... lib
"""
import os
import re
import subprocess
import sys

START = 0xA930
CODE_END = 0xB8E8
BSS_START = 0xB8E8
BSS_SIZE = 0x18


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
        "    OVL:  file = %%O, start = $%04X, size = $%04X;" % (START, CODE_END - START),
        "    OVLB: file = \"\", start = $%04X, size = $%04X;" % (BSS_START, BSS_SIZE),
        "}",
        "SEGMENTS {",
        "    OVLSTART: load = OVL,  type = ro;",
        "    CODE:     load = OVL,  type = ro;",
        "    RODATA:   load = OVL,  type = ro;",
        "    DATA:     load = OVL,  type = rw;",
        "    BSS:      load = OVLB, type = bss, define = yes;",
        "}",
        "SYMBOLS {",
    ]
    for name in sorted(imports):
        addr = imports[name]
        if addr < 0x100:
            lines.append("    %s: type = export, value = $%04X, addrsize = zeropage;" % (name, addr))
        else:
            lines.append("    %s: type = export, value = $%04X;" % (name, addr))
    lines.append("}")
    lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def problems(text):
    bad = set(re.findall(r"Unresolved external '([^']+)'", text))
    bad |= set(re.findall(r'Unresolved external "([^"]+)"', text))
    return bad


def main():
    if "--" not in sys.argv:
        sys.stderr.write("usage: linkbas.py LABEL OUT MAP CFG -- objs...\n")
        return 1
    sep = sys.argv.index("--")
    label, out_path, map_path, cfg_path = sys.argv[1:sep]
    objs = sys.argv[sep + 1:]
    libs = [o for o in objs if o.endswith(".lib")]
    objs = [o for o in objs if o not in libs]
    labs = load_lbl(label)
    imports = {}
    use_libs = False
    for _ in range(12):
        write_cfg(cfg_path, imports)
        cmd = ["ld65", "-C", cfg_path, "-o", out_path, "-m", map_path] + objs
        if use_libs:
            cmd += libs
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        text = proc.stdout or ""
        if proc.returncode == 0:
            break
        missing = problems(text)
        added = False
        for name in missing:
            if name in labs and name not in imports:
                imports[name] = labs[name]
                added = True
        if missing and not use_libs:
            use_libs = True
            added = True
        if not added:
            sys.stderr.write(text)
            return 1
    else:
        sys.stderr.write("linkbas: too many passes\n")
        return 1
    end = START
    bss_end = BSS_START
    for line in open(map_path):
        parts = line.split()
        if len(parts) >= 3 and parts[0] in ("OVLSTART", "CODE", "RODATA", "DATA", "BSS"):
            try:
                last = int(parts[2], 16) + 1
            except ValueError:
                continue
            if parts[0] == "BSS":
                bss_end = max(bss_end, last)
            else:
                end = max(end, last)
    if end > CODE_END or bss_end > BSS_START + BSS_SIZE:
        sys.stderr.write("linkbas: overlay uses $%04X-$%04X, room is $%04X-$%04X and BSS to $%04X\n" % (
            START, end, START, CODE_END, BSS_START + BSS_SIZE))
        return 1
    sys.stdout.write("SMBGET.OVL %u bytes at $%04X\n" % (os.path.getsize(out_path), START))
    return 0


if __name__ == "__main__":
    sys.exit(main())
