# SMBGET User Manual

Elmars Ositis

SMBGET copies files between an Apple II and a folder shared by a modern computer. The screen is two lists, in the style of Norton Commander. The left list is a ProDOS volume. The right list is an SMB share. You can also point both lists at ProDOS volumes and copy between them.

The program speaks SMB version 2.0.2. It signs in as a guest, with no user name and no password. It connects to an IP address on TCP port 445. It does not look up names, and it does not use SMB 3, encryption, or packet signing. The share has to accept that kind of guest.

## What you need

- An enhanced Apple IIe, an Apple IIc, an Apple IIc Plus, or an Apple IIgs, with 128K of memory and an 80-column display.
- One of these network devices on the same Ethernet network as the computer that holds the share:
  - a Uthernet II
  - a MegaFlash, which provides the network on an Apple IIc or IIc Plus
  - a CoGS
- A DHCP server, which a home router already is. SMBGET asks the router for an address when you connect.
- The SMBGET disk, or the same files copied into one ProDOS directory:

| File | What it is |
| --- | --- |
| `SMBGET.SYSTEM` | The program. ProDOS runs this when you boot the supplied disk. |
| `SMBGET.NAM` | The name tables. The program stops at startup if this file is missing. |
| `SMBGET.UTH` | The Uthernet II and MegaFlash driver. |
| `SMBGET.COG` | The CoGS driver. |
| `SMBGET.OVL` | Turns Applesoft programs into text listings, and listings back into programs. Copying a `.bas` file asks for this file. |

Keep those files together in the directory you launch from. On the supplied disk they already sit next to `PRODOS`.

At startup SMBGET disconnects the ProDOS `/RAM` disk so the auxiliary memory is free for the file lists. That RAM disk returns the next time you boot. On an Apple IIgs, `/RAM5` is left in place.

## A first session

1. Connect the Apple II to the same network as the sharing computer. On an Apple IIc Plus with MegaFlash, leave the machine at its normal fast speed. SMBGET waits for the network chip itself.
2. Boot the disk, or run `SMBGET.SYSTEM`.
3. The left list opens on the volume you started from. The right list is the share, and it is empty until you connect.
4. Press `N`.
5. The program looks for a network card, then asks the router for an address. The screen shows `DHCP: discover...` and then the address it received. A message of `No DHCP reply (cable/server?)` means the cable, the router, or DHCP. Check those before changing anything else.
6. Type the sharing computer’s IP address, such as `192.168.1.20`. A name such as `living-room.local` will not work.
7. Type the share name, such as `apple2`. Type the name alone. Leave out `\\`, the IP address, and slashes.
8. The right list fills with the share. The title bar shows `SMB host:` and the address.

The card search prefers a CoGS when one is installed. Otherwise it looks for a Uthernet II or a MegaFlash. On an Apple IIc, IIc Plus, or IIgs the usual Uthernet slot is slot 4. On an Apple IIe it is slot 3. Disk II slots are skipped. There is no setup screen yet; `S` only tells you to use `N`.

## Moving around

Press `?` for the short help screen. The keys are:

| Key | Action |
| --- | --- |
| Tab | Switch to the other list |
| Up / Down | Move the cursor |
| `<` / `>` | Move a page |
| Return | Open the folder under the cursor |
| Left or Esc | Go up to the parent folder |
| Space | Tag the cursor line, then move down. A tagged line is marked with `*` |
| `A` | Tag every file and folder, or clear the tags if any are already set. The `<UP>` line is never tagged |
| `V` | Pick a ProDOS volume for the active list |
| `C` | Copy to the other list |
| `D` | Delete |
| `R` | Rename |
| `M` | Make a folder |
| `N` | Connect to a share |
| `Q` | Quit |
| Esc | During a copy, stop the copy |

Each row shows the ProDOS name, a three-letter type (`TXT`, `BIN`, `BAS`, `SYS`, and so on), the size in blocks, and a date as `DD-MMM-YY`.

`C` copies the tagged rows. When nothing is tagged, it copies the row under the cursor. A folder is copied with everything inside it. Files land in the folder open on the other side. Copying into the same folder is refused.

If the name is already there, the prompt is:

| Key | Action |
| --- | --- |
| `O` | Overwrite this file |
| `S` | Skip this file |
| `A` | Overwrite this file and later clashes |
| `N` | Skip this file and later clashes |
| `C` or Esc | Stop the copy |

A local-to-local copy keeps the ProDOS file type, the aux type, and the bytes unchanged. The translations below apply when one side is the share.

`D` asks before it deletes. On the share, a folder has to be empty before the server will remove it. `R` on a ProDOS volume asks for a ProDOS name of up to 15 characters. `R` on the share asks for a name of up to 40 characters, stored as you type it. `M` creates a folder in the list you are in.

## How a name changes

A ProDOS name is at most 15 characters, starts with a letter, and contains only letters, digits, and periods. SMBGET shows those names in uppercase.

On the way to the share, a file name is written in lowercase. A folder name is left as it appears in the list. On the way back, the name is forced into the ProDOS rules: letters become uppercase, any other character becomes a period, and a name that does not start with a letter gains an `X` at the front. Anything past 15 characters is cut off.

An older copy of SMBGET wrote names ending in `#TTAAAA`, six hex digits for the ProDOS type and aux type. SMBGET still understands that ending and removes it from the ProDOS name. The current program does not write it. The type travels by the file’s ending instead, as the matrix below describes. The exact aux type of a binary file, a picture, or a system program is not stored on the share.

## How the bytes change

Three kinds of file are rewritten. Everything else is copied byte for byte.

### Plain text

This is a ProDOS `TXT` file whose aux type is `$0000`, or a share file that SMBGET has decided is text.

On the way to the share, the high bit of every byte is cleared, and each Return (`CR`, byte 13) becomes a line feed (`LF`, byte 10). A modern editor then sees ordinary ASCII with Unix line endings. Apple II text often has the high bit set; clearing it is what makes the file readable on the other computer.

On the way to the Apple II, a line feed becomes a Return. A Return that is already in the file is removed, so a Windows `CRLF` pair becomes one ProDOS Return. The high bit is left as it arrived.

Put line feeds, or Windows `CRLF`, in text you place on the share yourself. A file that uses only Returns, as classic Mac OS did, loses its line breaks on the way to ProDOS, because those Returns are removed and there is no line feed to put one back.

A text file whose aux type is not `$0000` is still given a `.txt` name. The bytes are copied unchanged on the way up. The next download sees `.txt` and treats it as aux type `$0000`, so the line endings are converted then.

### Applesoft BASIC

A ProDOS `BAS` file is a tokenized Applesoft program. On the share it becomes a text listing, one line per line feed, in the same form Applesoft uses when you `LIST`:

```
10  PRINT "HELLO"
20  GOTO 10
```

Keywords come from the Apple II’s Applesoft ROM. `SMBGET.OVL` has to be next to `SMBGET.SYSTEM`. Without it the copy stops and the screen says `Need SMBGET.OVL`.

On the way back, the listing is tokenized into an Applesoft program with aux type `$0801`, which is the normal load address. Line feeds and Returns both end a line. A blank line, or a line with no line number, is skipped. A line longer than 239 characters is cut off. Integer BASIC is not converted.

### Everything else

Binary files, system programs, pictures, and types SMBGET does not name are copied as raw bytes. The name still changes, and the ProDOS type on the way back is the one in the matrix, not the original type.

One exception: if a file would become a `BIN` at aux type `$0800`, and the first block contains only printable characters, Tab, Return, and line feed, SMBGET saves it as plain text and applies the text rules. A binary file that happens to look like text at the start will be rewritten. Give such a file one of the binary endings in the matrix (`.bin`, `.dsk`, `.po`, and the others) so the type is decided by the name.

## File type matrix

The type column is what the Apple II list shows. “Share name” is the ending added when the ProDOS name does not already have a matching one.

| ProDOS type | Aux type SMBGET assigns on download | Ending on the share | What happens to the bytes |
| --- | --- | --- | --- |
| `TXT` | `$0000` | `.txt` (`.text` is also recognized; `.md`, `.s`, `.c`, and `.h` are kept and still mean text) | High bit cleared and Return becomes line feed on the way up. Line feed and `CRLF` become Return on the way down |
| `BAS` | `$0801` | `.bas` | Tokenized program becomes a text listing on the way up. A listing becomes a tokenized program on the way down. Requires `SMBGET.OVL` |
| `SYS` | `$2000` | `.system` (`.sys` is kept) | Raw bytes. A system program comes back as `SYS` at aux `$2000`, which is the usual start address |
| `BIN` | `$0800` | `.bin` | Raw bytes, unless the first block looks like text, in which case it is saved as `TXT` |
| Disk image | `$0800`, shown as `BIN` | `.po`, `.do`, `.dsk`, `.hdv`, `.2mg` are kept | Raw bytes. The ending is preserved in the ProDOS name |
| `DIR` | folder | no ending | The folder and its contents. The folder name is not lowercased |
| `FOT`, `S16`, `INT`, `IVR`, `VAR`, `REL`, and any other type | `$0800`, shown as `BIN` | `.bin` | Raw bytes. The original type and aux type are not restored |

A file on the share with no ending, such as `shakespeare`, is taken to be plain text.

A name that already ends in `#TTAAAA` from an older SMBGET uses that type and aux type, and the ending is not part of the ProDOS name.

## Dates

On the way to the Apple II, the file’s modification date on the server becomes the ProDOS date. Years before 1940 or after 2039 are stored as no date. The time of day is not copied.

On the way to the share, the server sets the modification time to the moment of the copy. The ProDOS date is not sent. A local-to-local copy leaves the ProDOS date and time as the create call recorded them.

## Sharing a folder

Create one folder for the Apple II and share that folder, rather than a whole disk. The name you type at `Share name` is the SMB share name.

The Apple II and the sharing computer need addresses on the same network. Find the sharing computer’s address before you press `N`:

- macOS: System Settings, Network, the active connection. The address looks like `192.168.1.20`.
- Windows: Settings, Network & internet, the active connection, or `ipconfig` in a command prompt. Use the IPv4 address.
- Synology: Control Panel, Network, Network Interface, or the address on the DSM sign-in page.

### macOS

These steps match System Settings on current macOS. Older releases use System Preferences, Sharing, with the same choices.

1. System Settings, General, Sharing. Turn on File Sharing.
2. Click the Info button next to File Sharing. Turn on “Share files and folders using SMB.”
3. Add the folder. Set Everyone to Read & Write if the Apple II should be able to write.
4. System Settings, Users & Groups, Guest User. Turn on “Allow guest users to connect to shared folders.”
5. In the Finder, select the folder, choose Get Info, and under Sharing & Permissions set Everyone to Read & Write.

macOS often refuses a guest that does not sign its packets. SMBGET does not sign. On the Mac, this allows the unsigned guest session, then restarts file sharing:

```
sudo defaults write /Library/Preferences/SystemConfiguration/com.apple.smb.server SigningRequired -bool NO
```

Turn File Sharing off and back on after that command. If the Apple II says `Guest login refused`, this setting or the Guest User switch is the first place to look.

### Windows

Windows 10 and Windows 11 refuse a guest unless you allow it, and they refuse the session if signing is required.

1. Put the files in a folder. Right-click the folder, Properties, Sharing, Advanced Sharing. Turn on “Share this folder.” The share name is what SMBGET asks for.
2. Click Permissions and give Everyone Change and Read. In the Security tab, give Everyone Modify as well, or the copy will fail when ProDOS tries to create a file.
3. Control Panel, Network and Sharing Center, Advanced sharing settings, All Networks. Turn off password protected sharing. That is what lets a guest in.
4. Leave SMB 2 enabled. SMBGET does not use SMB 1, and it does not need SMB 1 turned on.
5. In Local Security Policy, or in `gpedit.msc` when the edition includes it, set “Microsoft network server: Digitally sign communications (always)” to Disabled. “Digitally sign communications (if client agrees)” can stay Enabled. Home editions hide this policy; the default there already does not require signing.

If the session is still refused, Windows is blocking insecure guest logons. In `gpedit.msc` enable “Enable insecure guest logons” under Computer Configuration, Administrative Templates, Network, Lanman Workstation. The same switch is the registry value `AllowInsecureGuestAuth` (a DWORD set to `1`) under `HKLM\SYSTEM\CurrentControlSet\Services\LanmanWorkstation\Parameters`. Restart the computer after changing it.

### Synology DSM

1. Control Panel, File Services, SMB. Turn SMB on.
2. Open Advanced Settings. Set the minimum protocol to SMB2. SMB3 can remain the maximum. SMBGET only offers 2.0.2, so a minimum of SMB3 will fail at `SMB2 negotiate failed`.
3. Set transport encryption to Disable. SMBGET does not encrypt.
4. Set server signing so the server does not force signatures. “Disable” or a client-defined choice is suitable. “Force” produces `Guest login refused` or a failed negotiate.
5. Control Panel, User & Group. Enable the Guest account.
6. Control Panel, Shared Folder. Edit the folder, Permissions, and give the guest Read/Write. Apply the permission to the files already in the folder.

The share name DSM shows, not the volume path, is what you type into SMBGET.

## When something fails

The connect screen names the step it reached.

| Screen | What to check |
| --- | --- |
| `No network card` | The Uthernet II, MegaFlash, or CoGS is seated, and `SMBGET.UTH` or `SMBGET.COG` is next to `SMBGET.SYSTEM` |
| `No DHCP reply (cable/server?)` | The Ethernet cable, link lights, and the router’s DHCP |
| `TCP connection failed` | The IP address, port 445, and a firewall allowing SMB on the local network |
| `SMB2 negotiate failed` | The server is offering SMB 2.0.2. A minimum of SMB 3, or required encryption, stops here |
| `Guest login refused` | Guest access, and signing set so that it is not required |
| `Share not accessible` | The share name, and the guest’s permission to open that folder |
| `Need SMBGET.OVL` | `SMBGET.OVL` is missing from the program’s directory, and you copied a `.bas` file |
| `Path too long` | The ProDOS path grew past 64 characters. Use a shorter folder name or a shallower tree |
| `Copy failed` / `Server error` | The guest can create and change files, and the volume or the share has free space |

Press Esc to abandon a copy that is already running. Files copied before that point stay.

## Reporting a problem

Please report problems in the repository:

https://github.com/eositis/smbget/issues

A useful report names the Apple II model, the network card, the computer that hosts the share, and the exact line SMBGET printed (`DHCP: discover...`, `Guest login refused`, `Copy failed`, and the rest). If a file arrived changed, include its ProDOS type and the name it had on the share.
