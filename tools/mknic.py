#!/usr/bin/env python3
"""Link a NIC overlay at the main program's nic_hole and import its calls.

The overlay is a raw binary whose first bytes are struct nic. Symbols the
main program already defines (runtime, UI, buffers, timer) are imported at
those addresses so the hole does not grow a second copy. Passes:

  1. Link with a fixed set of imports (timer, ethernet buffers, zero page)
     plus the libraries named on the command line.
  2. Any unresolved symbol that exists in the main label file is imported
     and the link is repeated.
  3. After a successful link, symbols that landed inside the hole and also
     exist in the main program are imported too, and the link is repeated.
     That drops libc copies the hole does not have room for.

Usage:
  mknic.py LABEL HOLE_SIZE OUT MAP CFG -- obj.o ... lib.lib
"""
import os
import re
import struct
import subprocess
import sys

FORCE = [
    "sp", "sreg", "regsave", "regbank",
    "ptr1", "ptr2", "ptr3", "ptr4",
    "tmp1", "tmp2", "tmp3", "tmp4",
    "timer_jiffy", "_timer_jiffy",
    "timer_seconds", "timer_timeout", "_timer_timeout",
    "eth_inp", "eth_outp",
    "output_buffer", "_output_buffer",
    "abort_key", "_abort_key",
    "check_for_abort_key", "_input_check_for_abort_key",
    "addysp", "addysp1",
    "_net",
    "timer_jiffy", "_timer_jiffy",
    "timer_seconds",
]


def load_lbl(path):
    labs = {}
    for line in open(path):
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "al":
            name = parts[2][1:] if parts[2].startswith(".") else parts[2]
            labs[name] = int(parts[1], 16)
    return labs


def write_cfg(path, hole, size, imports):
    lines = [
        "MEMORY {",
        "    OV: start = $%04X, size = $%04X, file = %%O;" % (hole, size),
        "}",
        "SEGMENTS {",
        "    CODE:   load = OV, type = ro;",
        "    RODATA: load = OV, type = ro;",
        "    DATA:   load = OV, type = rw;",
        "    BSS:    load = OV, type = bss, define = yes;",
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


def in_overlay(addr, hole):
    """Anything the overlay linked at or above the hole, including a
    trial link that was allowed to run past the real hole."""
    return addr >= hole


def module_symbols(map_text, symbol):
    """Names defined by the same library object as symbol."""
    match = re.search(r"(?m)^%s \(([^)]+)\):" % re.escape(symbol), map_text)
    if not match:
        return []
    module = match.group(1)
    names = []
    for found in re.finditer(r"(?m)^(\S+) \(([^)]+)\):", map_text):
        if found.group(2) == module:
            names.append(found.group(1))
    return names


def main():
    if "--" not in sys.argv:
        sys.stderr.write("usage: mknic.py LABEL HOLE_SIZE OUT MAP CFG -- objs...\n")
        return 1
    sep = sys.argv.index("--")
    label, size_s, out_path, map_path, cfg_path = sys.argv[1:sep]
    objs = sys.argv[sep + 1:]
    if not objs:
        sys.stderr.write("mknic: no objects\n")
        return 1
    size = int(size_s, 0)
    libs = [o for o in objs if o.endswith(".lib") or o.endswith(".a")]
    objs = [o for o in objs if o not in libs]
    main_labs = load_lbl(label)
    if "_nic_hole" not in main_labs:
        sys.stderr.write("mknic: _nic_hole missing from %s\n" % label)
        return 1
    hole = main_labs["_nic_hole"]
    imports = {}
    for name in FORCE:
        if name in main_labs:
            imports[name] = main_labs[name]
    banned = set()
    lbl_path = out_path + ".lbl"
    use_libs = False

    for attempt in range(12):
        write_cfg(cfg_path, hole, max(size, 0x5000), imports)
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
                if name in main_labs and in_overlay(addr, hole):
                    extra[name] = main_labs[name]
            if not extra:
                break
            imports.update(extra)
            continue
        unresolved, dups = parse_problems(text)
        changed = False
        map_text = ""
        if os.path.exists(map_path):
            map_text = open(map_path).read()
        for name in dups:
            if name not in imports:
                continue
            pulled = False
            for sib in module_symbols(map_text, name):
                if sib in main_labs and sib not in imports and sib not in banned:
                    imports[sib] = main_labs[sib]
                    pulled = True
                    changed = True
            if not pulled:
                del imports[name]
                banned.add(name)
                changed = True
        missing = False
        for name in unresolved:
            if name in main_labs and name not in imports and name not in banned:
                imports[name] = main_labs[name]
                changed = True
            elif name not in main_labs:
                missing = True
        if missing and not use_libs:
            use_libs = True
            changed = True
        if not changed:
            sys.stderr.write(text)
            sys.stderr.write("mknic: link failed for %s\n" % out_path)
            return 1
    else:
        sys.stderr.write("mknic: too many passes for %s\n" % out_path)
        return 1

    used = hole
    for line in open(map_path):
        parts = line.split()
        if len(parts) >= 3 and parts[0] in ("CODE", "RODATA", "DATA", "BSS"):
            try:
                end = int(parts[2], 16) + 1
            except ValueError:
                continue
            if end > used:
                used = end
    if used - hole > size:
        sys.stderr.write("mknic: %s needs %u bytes, hole is %u\n" % (
            os.path.basename(out_path), used - hole, size))
        return 1
    data = open(out_path, "rb").read()
    if data[:4] != b"NIC1":
        sys.stderr.write("mknic: %s does not start with NIC1\n" % out_path)
        return 1
    if len(data) > size:
        sys.stderr.write("mknic: %s is %u bytes, hole is %u\n" % (
            out_path, len(data), size))
        return 1
    patched = data[:6] + struct.pack("<H", len(data)) + data[8:]
    with open(out_path, "wb") as f:
        f.write(patched)
    sys.stdout.write("%s %u bytes at $%04X (%u import%s)\n" % (
        os.path.basename(out_path), len(patched), hole, len(imports),
        "" if len(imports) == 1 else "s"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
