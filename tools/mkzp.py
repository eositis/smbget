#!/usr/bin/env python3
"""Write an assembler file exporting the cc65 runtime zero-page symbols at the addresses the main
program was linked with (read from its VICE label file), so the overlay shares them."""
import re, sys

NAMES = "sp sreg regsave ptr1 ptr2 ptr3 ptr4 tmp1 tmp2 tmp3 tmp4 regbank".split()
labels = {}
for line in open(sys.argv[1]):
    m = re.match(r"al\s+([0-9A-Fa-f]+)\s+\.(\w+)", line)
    if m:
        labels[m.group(2)] = int(m.group(1), 16)
missing = [n for n in NAMES if n not in labels]
if missing:
    sys.exit("mkzp: not in label file: " + " ".join(missing))
with open(sys.argv[2], "w") as f:
    for n in NAMES:
        f.write("        .exportzp %s = $%02X\n" % (n, labels[n]))
