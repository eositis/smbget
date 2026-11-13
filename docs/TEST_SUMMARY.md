# SMBGET Test Summary (Build b033)

## Host Unit Tests (All Passing)

```
$ make test
/Library/Developer/CommandLineTools/usr/bin/make -C tests/host test
build/test_names
all names tests passed
build/test_bas
all bas tests passed
build/test_ovl >/dev/null && echo "overlay glue tests passed"
overlay glue tests passed
python3 tools/check_cc65_bug.py ...
cc65 miscompile scan: 0 suspect(s)
```

| Test | Pass? | Notes |
|------|-------|-------|
| `test_names.c` | ✅ | ProDOS↔host name mapping (`#TTAAAA`) |
| `test_bas.c` | ✅ | BASIC tokenization/detokenization |
| `test_ovl.c` | ✅ | Overlay glue (`OVL_UP`/`DOWN_*` ops) |
| `check_cc65_bug.py` | ✅ | No miscompiles in full C sources |

## Build Artifacts

| File | Size | Timestamp | Status |
|------|------|-----------|--------|
| `build/SMBGET.SYSTEM` | 35,886 B | 2026-11-02 23:16:08 | Fresh |
| `build/SMBGET.OVL` | 4,725 B | 2026-11-02 23:16:08 | Fresh |
| `build/smbget.po` | 143,360 B | 2026-11-02 23:16:08 | Fresh |
| `dist/smbget.system` | 36,092 B | 2026-11-02 22:44:16 | Stale |
| `dist/smbget.ovl` | 4,728 B | 2026-11-02 22:44:16 | Stale |

**Note**: `dist/` is stale (older than `build/`). Run `make dist` to update.

---

## Hardware Readiness Checklist

| Component | Status | Notes |
|-----------|--------|-------|
| cc65 toolchain | ✅ | v2.18, `apple2enh` target |
| Uthernet II/W5100 driver | ✅ | DHCP + TCP via hardware engine |
| SMB2 guest client | ✅ | Dialect 2.0.2, NTLMSSP |
| Overlay architecture | ✅ | BASIC conversion glue tested on host |
| Dual-pane UI | ✅ | ProDOS + SMB panes wired |
| ProDOS volume picker | ✅ | Multi-volume support |
| `#TTAAAA` name mapping | ✅ | Round-trip type/aux preserved |

## Known Gaps for Hardware Launch

| Feature | Status | Workaround |
|---------|--------|------------|
| Setup screen | ❌ Not built | Network config via `N` only |
| SMB delete/rename/mkdir | ❌ Local-only | Manual SMB host operations |
| Folder transfer | ❌ Skipped | Copy files individually |
| Real hardware validation | ⚠️ Unrun | Use emulator (AppleCommander, Virtual II) first |

---

## Quick Start (Host Developer)

```bash
# Build
brew install cc65
make

# Run host tests
make test

# Generate fresh ProDOS disk image
make dist  # copies to dist/
make       # rebuilds build/smbget.po
```

## Quick Start (Apple II Hardware)

1. Write `build/smbget.po` to 140K ProDOS disk.
2. Insert Uthernet II in Slot 3 (or configure slot in `src/main.c` `net_start`).
3. Boot the disk — SMBGET.SYSTEM starts automatically.
4. Press `N`, enter SMB server IP and guest share name.
5. Copy files between panes.

---

## Files & Lines of Code (C + ASM, excluding ip65 vendor)

| File | Lines | Purpose |
|------|-------|---------|
| `src/main.c` | 517 | UI, key dispatch, pane management |
| `src/prodos.c` | 310 | ProDOS volume/directory/MLI glue |
| `src/ui.c` | 244 | Text drawing, input, alerts |
| `src/pane.c` | 228 | Pane listing & navigation |
| `src/copy.c` | 270 | File copy with progress/abort |
| `src/names.c` | 162 | `#TTAAAA` ProDOS↔host mapping |
| `src/net.c` | 146 | Uthernet II, DHCP, TCP glue |
| `src/smb2.c` | 508 | SMB2 guest client (dialect 2.0.2) |
| `src/w5100.c` | 421 | W5100 hardware driver |
| `src/ovl/ovl.c` | 128 | Overlay glue |
| `src/ovl/bas.c` | 172 | BASIC conversion |
| ASM (total) | ~1,200 | MLI, auxmem, IP65 glue, ethdrv |
| **Total (est.)** | ~3,600 | All production source |

---

*Generated for build b033, 2026-11-03*
