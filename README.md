# SMBGET

Norton-Commander-style dual-pane file copier for the Apple II: local ProDOS volumes on the left, an SMB share on the right. Goal: easy file transfer between Apple II systems and modern storage.

Target: enhanced //e, //c, //c+ or IIgs with 128K, 80-column text, and a Uthernet II, MegaFlash, or CoGS. Built with cc65.

The user manual is [docs/MANUAL.md](docs/MANUAL.md). It covers setup, guest shares on macOS, Windows, and Synology, and the file-type rules. Problems and requests go to https://github.com/eositis/smbget/issues.

## Build
    brew install cc65
    make            # build/SMBGET.SYSTEM and build/smbget.po (volume with SMBGET.SYSTEM)
    make test       # host unit tests (names mapping)

`tools/mkpo.py` is a dependency-free ProDOS image tool: `new`, `add`, `ls`. To test, add `SMBGET.SYSTEM` (type `FF`, aux `2000`) to a bootable ProDOS image:

    python3 tools/mkpo.py add MYDISK.po build/SMBGET.SYSTEM --type FF --aux 2000

## Keys
TAB pane, arrows move, RET open, LEFT/ESC parent, SPACE tag, A tag all, V volumes, C copy, D delete, R rename, M mkdir, Q quit.
