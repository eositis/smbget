# Apple IIc Plus W5100 timing

MegaFlash presents a virtual W5100 in slot 4. On an Apple IIc Plus that slot stays at 4 MHz. The same SMBGET binary gets a DHCP lease and a TCP connection at 1 MHz, and fails both at 4 MHz, until the waits below are in place.

Slot 4 must stay fast. `$C05A` and `$C05C` are not written. `$C0C0`–`$C0C3` are MegaFlash’s command, parameter, and status registers (system storage, the millisecond timer, floating point). They are meant to run at 4 MHz. The virtual W5100 window is the next four addresses, `$C0C4`–`$C0C7`: mode, address high, address low, data. Those stay at 4 MHz too. Software waits for MegaFlash to publish the next data byte.

## What the 6502 actually samples

MegaFlash prepares the data-port byte on the bus cycle before the 6502 reads it. At 1 MHz the instructions between an address write and the data access are long enough. At 4 MHz the next access often latches the previous byte.

Mode `$03` is indirect access with auto-increment. A second `lda data` or `sta data` without writing the address again is the next register or the next buffer byte, not another sample of the same one. A real repeat read writes the address again, then reads the data port again.

A stale byte shows up in three ways:

- DHCP never leaves “DHCP: discover…”. `send_dhcpdiscover` is already running. The socket command register (`Sn_CR`, socket 0 offset `$01`) never reads as 0, so the send loop never finishes. A dead `$C019` timer looks the same, but the timer is a separate problem (see below).
- DHCP succeeds and TCP connect fails. Socket status comes back 0 while `Sn_CR` is still busy, and connect gives up.
- DHCP works on some boots and not others. The first byte of a 16-bit register was waited for. The second byte followed about 1 µs later and was still the previous value. A bad MAC-raw length, RX size, TX free size, or buffer pointer drops the frame.

## The wait

One routine, used by the IP65 MAC-raw driver and by the C TCP driver. It runs only on an IIc Plus. Every other machine pays a few cycles to test the flag and return. It preserves A, X, and Y: callers still need A after `set_addr`, and the write loop has the byte in A and the index in Y.

IIc Plus is `$FBB3 == 6`, `$FBC0 != $E0`, `$FBBF == 5`. `$FBB3 == 6` with `$FBBF == 4` is a ROM 4 IIc and runs at 1 MHz, so it does not take the wait. `$FBC0 == $E0` is a IIe; `$C05A` on that machine is an annunciator, which is another reason this code never touches it.

`iic_plus` lives in BSS. The overlay is cleared before the driver is loaded, so the flag starts at 0. `init` does `inc iic_plus` and does not store 0 first. A second `ip65_init` in the same load leaves the flag nonzero.

```asm
.pc02

init:
        pha
        lda     $FBB3
        cmp     #6
        bne     @doneplus
        lda     $FBC0
        cmp     #$E0
        beq     @doneplus
        lda     $FBBF
        cmp     #5
        bne     @doneplus
        inc     iic_plus
@doneplus:
        pla
        ; ... slot fixups follow, unchanged ...

.export _w5100_pace
_w5100_pace = pace
pace:
        pha
        lda     iic_plus
        beq     @rest
        phx
        phy
        ldy     #80          ; ~400 cycles, ~100 us at 4 MHz
:       dey
        bne     :-
        ply
        plx
@rest:  pla
        rts

.bss
iic_plus: .res 1
```

`ldy #20` (~25 µs) was enough for TCP once every data byte was paced. DHCP stayed intermittent until the count was `#80` and the 16-bit pairs below waited between their two accesses. The immediate can grow later without changing the size of the routine. `.pc02` has to be in force before `phx` / `phy`; ca65’s default CPU rejects them.

Do not call ROM `WAIT` (`$FCA8`). Do not touch `$C080`.

## Assembly driver (DHCP, MAC raw)

SMBGET compiles `ext/ip65/drivers/uthernet2.s`, which sets `DYN_DRV = 0` and includes `w5100.s`. That object is linked ahead of `ip65_apple2_uther2.lib`, so the library’s copy of the driver is not pulled. Assemble with `ca65 -I ip65/inc -I cc65/asminc` and without `-t apple2enh`: `uthernet2.s` already defines `__APPLE2__`.

`set_addr` ends with `jsr pace` after the two address stores. That covers the first data access that follows. It does not cover a second access a few cycles later.

### Wait between the two bytes

These pairs are two different bytes. Insert `jsr pace` between them. `pace` preserves A, X, and Y, so the byte or pointer register already loaded is still valid. The `fixupNN` labels are single-byte deltas in the slot-patch table; three extra bytes between a pair stays inside that limit, but the file has to assemble cleanly after the edit.

| Site | Access | Why a stale second byte matters |
| --- | --- | --- |
| `fixup03` / `fixup04` | `eor data` / `eor data` | RTR fingerprint `$0017`/`$0018`. Failure looks like “device not found”. |
| `fixup10` / `fixup11` | `sta data` / `sta data` | RX and TX memory size, same value written twice. |
| `fixup13` / `fixup14` | `sta data`, `lda #$01`, `sta data` | Socket mode, then `Sn_CR = OPEN`. Pace after the `lda #$01` so A is still `$01`. |
| `fixup16` / `fixup17` | `lda data` / `ora data` | RX received size. A stale low byte makes “is there a frame?” come and go. |
| `fixup18` / `fixup19` | `sta data` / `stx data` | RX or TX pointer update. |
| `fixup22` / `fixup23` | `lda data` / `ldx data` | TX free size. A stale low byte makes the send wait compare fail, or never finish. |
| `fixup24` / `fixup25` | `lda data` / `ldy data` | Physical address of the frame. A bad pointer reads the wrong bytes. |
| `get_datacheckaddr` | `jsr pace` then `lda data` | Called twice for the MAC-raw length. The gap between the two calls is only `iny` / `rts` / `jsr` / `lda`, about 5 µs. A bad length desynchronizes every later DHCP byte. X and Y are the address shadow; `pace` must keep them. |

`mov_data` already paces every payload byte: `jsr pace` / `lda data` on read, and `lda (ptr),y` / `jsr pace` / `sta data` on write.

### Read `Sn_CR` twice

`Sn_CR` is one byte. A second `lda data` without a new address steps off it. Both polls (`poll` before the RX size check, and `send` before the TX free-size check) accept 0 only when a second read, after `jsr set_addr`, is also 0.

`set_addrcmdreg0` leaves the register address in X and Y. `set_addr` stores those and does not change them, so the confirm is `jsr set_addr` / `lda data`.

A stale nonzero only retries. A stale zero is the harmful case: the driver continues while the command is still pending. RX:

```asm
poll:
        jsr set_addrcmdreg0
fixup15:lda data
        bne @none
        jsr set_addr
        lda data
        beq :+
@none:  lda #$00
:       tax
        sec
        rts
:       ldy #$26
        ; RX received size, with jsr pace between the two bytes
```

TX:

```asm
:       jsr set_addrcmdreg0
fixup21:lda data
        bne :-
        jsr set_addr
        lda data
        bne :-
:       ldy #$20
        ; TX free size, with jsr pace between the two bytes
```

The anonymous labels are easy to point at the wrong place. The zero-size path in `poll` must still return “no data”. The free-size `bcc` must still retry the size read, not the command wait.

## C driver (TCP, after `w5100_config`)

IP65 is not on this path. `src/w5100.c` and `src/nic_uth.c` call the same `pace` through `void w5100_pace(void)`.

`set_addr` writes the address, then calls `w5100_pace()`. cc65 `-Os` deletes `(void)*w5100_addr_lo` even though the pointer is volatile. The 1 MHz build already lacked that latch read. The wait after the address stores is the part that matters. Reading the address port does not auto-increment.

`get_byte` and `set_byte` go through `set_addr`, so each one-byte register access waits. `set_word` and `set_quad` are one `set_byte` per byte. SMBGET’s `get_word` is still one `set_addr` and two data reads, because the overlay had no room for a second address setup. The compiler’s `pushax` between those reads is only a few microseconds. Prefer a2chat’s form, which already does `get_byte(addr)` and `get_byte(addr + 1)`.

After `w5100_send_request` or `w5100_receive_request` the data port auto-increments. Each byte of the copy needs its own wait:

```c
for (i = 0; i < snd; i++) {
    w5100_pace();
    *data = p[i];
}
```

```c
for (i = 0; i < avail; i++) {
    w5100_pace();
    p[i] = *data;
}
```

`sock_sr` reads socket status twice and a third time when the two samples differ. Each `get_byte` sets the address again, so each sample waits. Connect used to return failure when one stale 0 arrived while `Sn_CR` was still busy.

## What a2chat has to change

a2chat links the stock `ip65_apple2_uther2.lib` and its C loops write `*w5100_data` with no wait. The timer is already the MegaFlash millisecond counter on a IIc with MegaFlash (`src/ip65_timer.s`). Leave that alone. `$C019` does not advance on this IIc, so a VBL-only clock makes DHCP sit on “discover” even when the data port is fine.

1. Override the IP65 W5100 driver the way SMBGET does: compile `uthernet2.s` (so `DYN_DRV` is 0) and link that object before `ip65_apple2_uther2.lib`. Put `.pc02`, the IIc Plus test, `jsr pace` at the sites in the tables above, and the double `Sn_CR` read into that `w5100.s`. Export `_w5100_pace`.
2. `src/w5100.c` `set_addr`: call `w5100_pace()` after the address stores. Declare it in `w5100.h`. Keep `get_word` as two `get_byte` calls.
3. `src/w5100.c` `wait_cr`: accept 0 only when a second `get_byte` of `Sn_CR` is also 0. Each `get_byte` rewrites the address, which is the repeat read. A single stale 0 is what made TCP connect fail.
4. Pace every auto-increment data byte in `src/nic_uth.c` (`uth_send`, `uth_recv`) and in `src/w5100_http_post.c` (`send_bytes`, `recv_some`). A request sets the address once; the loop then walks the buffer.

CoGS does not use this window. The wait is inside the Uthernet / MegaFlash driver only.

## How to tell the stages apart

On the IIc Plus at 4 MHz, with slot 4 left fast:

| What is in the build | What happens |
| --- | --- |
| No wait | DHCP stays on “discover”. `Sn_CR` never looks idle. The same disk at 1 MHz leases and connects. |
| Wait only at `set_addr` and in `mov_data`, about 25 µs | DHCP often leases. TCP connect fails. |
| That wait also in C `set_addr` and in the C send/recv loops | TCP connects. DHCP still drops some attempts. |
| 100 µs, a wait between each 16-bit pair, and `Sn_CR` read twice | DHCP and TCP both complete. |

The 100 µs wait also runs on the TCP byte copy, because both sides call the same routine. That is intended.
