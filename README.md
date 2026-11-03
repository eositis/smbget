# SMBGET

Norton-Commander-style dual-pane file copier for the Apple II: local ProDOS volumes on the left, an SMB share (via Uthernet II) on the right. Goal: easy file transfer between Apple II systems and modern storage.

Target: enhanced //e, //c+ or IIgs with 128K, 80-column text. Built with cc65.

## Status
- Done: ProDOS side (volume picker, directory browsing, tag, copy, delete, rename, mkdir). Either pane can show a ProDOS volume, so local-to-local copy works as an engine test. `#TTAAAA` name mapping (type/aux preserved on SMB) with host unit tests.
- Not yet: Uthernet II (W5100) driver, SMB2 client, setup screen, SMB pane. See the plan for milestones.
- Not yet run on an emulator or real hardware (only built and linked).

## Build
    brew install cc65
    make            # build/SMBGET.SYSTEM and build/smbget.po (volume with SMBGET.SYSTEM)
    make test       # host unit tests (names mapping)

`tools/mkpo.py` is a dependency-free ProDOS image tool: `new`, `add`, `ls`. To test, add `SMBGET.SYSTEM` (type `FF`, aux `2000`) to a bootable ProDOS image:

    python3 tools/mkpo.py add MYDISK.po build/SMBGET.SYSTEM --type FF --aux 2000

## Keys
TAB pane, arrows move, RET open, LEFT/ESC parent, SPACE tag, A tag all, V volumes, C copy, D delete, R rename, M mkdir, Q quit.
