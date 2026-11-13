# Apple IIgs Hardware Test Plan for SMBGET

**Target**: Apple IIGS (128K min, 80-column text, enhanced //e or //c+ compatible mode also acceptable)  
**Build target**: `apple2enh` (cc65 `-t apple2enh`)  
**Build ID**: `b033` (current, bump after firmware flash)

---

## 1. Pre-Flight Checklist

### Hardware Requirements
- [ ] Apple IIGS with 128K+ RAM (expanded RAM OK)
- [ ] 80-column text card installed and functional
- [ ] Uthernet II Ethernet card in Slot 3 (or configured slot 1–7)
- [ ] Ethernet cable connected to a live network with SMB/CIFS server (Windows/macOS/Linux Samba)
- [ ] ProDOS 1.8 or newer bootable disk (FLASHVALID.po or equivalent boot blocks)
- [ ] Disk controller card (5.25" or 3.5" with appropriate media)

### Software Requirements
- [ ] `build/SMBGET.SYSTEM` — ProDOS SYS type `$FF`, aux `$2000`
- [ ] `build/SMBGET.OVL` — BIN aux `$0800`
- [ ] `build/smbget.po` — 140K ProDOS image with both binaries pre-seeded

---

## 2. Test Sequence

### 2.1 Basic Boot & Memory Check

| Step | Action | Expected Result | Diagnostic |
|------|--------|-----------------|------------|
| T1.1 | Boot `smbget.po` | ProDOS loader runs, SMBGET.SYSTEM starts | Screen should show “SMBGET needs a 128K Apple IIe/IIc (aux memory).” if aux missing |
| T1.2 | Verify 128K+ aux | If aux absent, press any key and exit | If present, screen clears and title draws |
| T1.3 | Observe title bar | ` SMBGET b033  ProDOS <-> SMB ` centered, “Net: not connected” | Build ID must match `BUILDNUM` |

### 2.2 ProDOS Volume Picker & Local Operations

| Step | Action | Expected Result | Diagnostic |
|------|--------|-----------------|------------|
| T2.1 | Press `V` in left pane | Volume picker shows available ProDOS volumes | Should show at least one volume (e.g., `FLASHVALID`) |
| T2.2 | Select volume, press `RET` | Pane loads directory listing | Files appear in 8-row scrollable list |
| T2.3 | Arrow keys navigation | Cursor moves up/down, `LIST_ROWS` visible | Up/Down arrows move 1 row; `<`/`>` jump by page |
| T2.4 | Press `D` on a file | “Not available for SMB yet” alert | Local delete still works, but `local_only()` guard must pass |
| T2.5 | Press `R` then `M` | Rename/Mkdir prompts appear | Requires local-only guard check; verify input validation |

### 2.3 Network Layer (Uthernet II)

| Step | Action | Expected Result | Diagnostic |
|------|--------|-----------------|------------|
| T3.1 | Press `N` | “Network test” dialog box | Log area at row 6–17 cleared and ready |
| T3.2 | Observe “Scanning for Uthernet II…” | After 1–3 seconds, “Uthernet II found in slot N” | `net_start()` scans slots 1–7 via `ip65_init()` |
| T3.3 | Wait for DHCP | “DHCP request sent” then “DHCP reply received” | Log shows IP address assigned; `net.up=1` |
| T3.4 | Check status line | “Net: 192.168.1.x” replaces “not connected” | `ip_str()` formats IP in title bar |

### 2.4 SMB2 Guest Connection

| Step | Action | Expected Result | Diagnostic |
|------|--------|-----------------|------------|
| T4.1 | Enter SMB server IP (e.g., `192.168.1.10`) | Prompt accepts dotted quad, accepts input | `ip_parse()` validates; invalid input re-prompts |
| T4.2 | Enter share name (e.g., `shared`) | “SMB connect” dialog, logs “Negotiate…” | `smb_connect()` sends negotiate with dialect `0x0202` |
| T4.3 | Observe session setup | “Guest session setup…” then “Tree connect…” | NTLMSSP NEGOTITE → AUTHENTICATE (guest), tree connect share |
| T4.4 | Directory read | “Reading directory…” then right pane shows `/` contents | `smb_readdir()` returns file/dir info |
| T4.5 | Verify right pane state | `panes[1].kind == PK_SMB`, `loaded=1` | Pane title shows `[ SMB / ]` |

### 2.5 SMB File Transfer (Download: SMB → ProDOS)

| Step | Action | Expected Result | Diagnostic |
|------|--------|-----------------|------------|
| T5.1 | Select a text file on right pane | Cursor highlights, confirm tag or use `SPC` | Tag state set |
| T5.2 | Press `C` to copy to left pane | “Copy: filename” status line | Progress bar `[####....]` 40 chars updates |
| T5.3 | Observe copy completion | “Files copied: 1” message | `copy_smb_down()` → `smb_open_file()` → read loop → BASIC detokenization if type `0xFC` |
| T5.4 | Check destination file | ProDOS file exists with correct type/aux | Use `PR#0` or ProDOS file manager to inspect |
| T5.5 | For BASIC file, verify syntax | `BRUN` the file — parses correctly | `bas_detok_line()` round-trip fidelity |

### 2.6 SMB File Transfer (Upload: ProDOS → SMB)

| Step | Action | Expected Result | Diagnostic |
|------|--------|-----------------|------------|
| T6.1 | Select a file in left pane, press `C` to right | “Copy: filename” status | Progress bar updates |
| T6.2 | For BASIC source, expect `#TTAAAA` suffix | SMB file named `TEST.TXT#04FC` | `names_prodos_to_host()` encodes type/aux |
| T6.3 | Observe upload completion | “Files copied: 1” message | `copy_smb_up()` → BASIC tokenization if type `0xFC` → write loop |
| T6.4 | On host, inspect file | Name matches `#TTAAAA`, type/aux match ProDOS | `python3 -c "print(hex(0xTT), hex(0xAAAA))"` |

### 2.7 Stress & Edge Cases

| Step | Action | Expected Result | Diagnostic |
|------|--------|-----------------|------------|
| T7.1 | Large file (>1400 bytes) | Progress bar continues, no truncation | `smb_read()` returns up to `SMB_READ_MAX=700` per chunk |
| T7.2 | Press `ESC` mid-copy | “Copy aborted” message, partial file removed | `ovl_aborted()` checked per chunk |
| T7.3 | Unplugged Ethernet mid-transfer | “Connection timed out” or “TCP closed” alert | `net_tcp_recv()` returns `-1`, `smb_status` set |
| T7.4 | Non-existent share | “Share not accessible” or “Server error” | `smb_errstr(SMB_E_TREE)` shows |
| T7.5 | Cross-panic local copy | Same as local-only copy, confirms engine integrity | `local_only()` returns `1`, no SMB paths |

### 2.8 Multi-Pane Navigation

| Step | Action | Expected Result | Diagnostic |
|------|--------|-----------------|------------|
| T8.1 | `TAB` key | Active pane cursor inverts, title bar inverts | `active ^= 1` toggles |
| T8.2 | `RET` on directory (left) | Pane path updates, subdirectory loads | `go_up()`/`open_cur()` handles `F_DIR` flag |
| T8.3 | `←/ESC` from root | Volume picker reappears | Path parent of `/` clears to empty |
| T8.4 | `A` tag-all then `C` copy | All tagged items copy with progress | Tag state persists across reloads |

---

## 3. Expected Log Messages (in order)

### Normal Boot
```
SMBGET b033  ProDOS <-> SMB
Scanning for Uthernet II...
Uthernet II found in slot 3
DHCP request sent...
DHCP reply received
Net: 192.168.1.42
```

### SMB Connect (guest)
```
SMB connect
Negotiate...
Guest session setup...
Tree connect...
Connected to share (guest).
Reading directory...
```

### Copy (SMB → ProDOS)
```
Files copied: 1
```

### Copy (ProDOS → SMB)
```
Files copied: 1
```

### Errors
```
Connection timed out
TCP connection failed
Share not accessible
Server error 0xC0000035
Path too long
```

---

## 4. Known Limitations to Note

- **No folder upload/download** — directories show “folders not copied yet” in copy summary.
- **Setup screen missing** — `S` key shows stub alert; network config via `N` only.
- **No SMB delete/rename/mkdir** — `D/R/M` guarded by `local_only()`; will alert “Not available for SMB yet”.

---

## 5. Success Criteria

A test pass requires:

1. **Boot**: SMBGET.SYSTEM starts on 128K+ Apple IIGS with 80-column text.
2. **Uthernet II**: DHCP completes, IP shown in title bar.
3. **SMB2**: Guest connect succeeds, directory lists correctly.
4. **Transfer**: Both directions complete with correct `#TTAAAA` encoding, BASIC files detokenize/tokenize.
5. **Recovery**: Error paths (timeout, missing share) display helpful `smb_errstr()` messages.
6. **No crashes**: 20+ minute stress test (repeated copy/transfer) with no memory corruption or reset.

---

## 6. Hardware Diagnostic Notes

| Symptom | Likely Cause |
|---------|--------------|
| Blank screen, no title | 128K aux not present, or AUXMEM glue miscompiled |
| “Uthernet II found in slot 0” | ip65 scan returns 0 (should be 1–7) → hardware init issue |
| DHCP never replies | Cable or DHCP server offline; check `net_errstr(NET_NODHCP)` |
| “Connection timed out” after slot scan | `w5100_config()` failed → W5100 chip not responding (slot wiring or W5100 defect) |
| SMB negotiate succeeds, session fails | NTLMSSP guest flags mismatch; `smb_stage=1/2` indicates step |
| “Path too long” mid-copy | Combined share path exceeds `PD_PATH_MAX=63` |

---

## 7. Version Tracking

- **Firmware**: `build/SMBGET.SYSTEM` (35,886 bytes, `b033`)
- **Overlay**: `build/SMBGET.OVL` (4,725 bytes, `b033`)
- **Image**: `build/smbget.po` (143,360 bytes, ProDOS 140K)
- **Build host**: macOS, cc65 v2.18, `make` target `apple2enh`

Update `BUILDNUM` after firmware flash and rebuild to bump `bXXX`.

---

*Generated for Apple IIGS hardware validation, 2026-11-03*
