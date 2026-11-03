#!/usr/bin/env python3
"""Scan the code cc65 2.19 generates for one known miscompile: in `x == K ? K2 : var` the else branch
reads `lda (sp),y` with Y still holding the previous offset, so it fetches the wrong local variable.
Write such code as if/else instead. Usage: check_cc65_bug.py [cc65 flags...] -- file.c ..."""
import subprocess, sys

args = sys.argv[1:]
i = args.index("--")
flags, files = args[:i], args[i + 1:]
bad = 0
for f in files:
    out = subprocess.run(["cc65"] + flags + ["-o", "-", f], capture_output=True, text=True).stdout
    ln = [l.strip() for l in out.splitlines()]
    for k in range(len(ln) - 4):
        if ln[k].startswith("ldy #") and ln[k + 1].startswith("cmp #") and ln[k + 2].startswith("bne") \
                and any(x == "lda (sp),y" for x in ln[k + 3:k + 7]):
            print("%s: possible miscompile near: %s" % (f, " / ".join(ln[k:k + 7])))
            bad += 1
print("cc65 miscompile scan: %d suspect(s)" % bad)
sys.exit(1 if bad else 0)
