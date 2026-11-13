;
; CoGS register primitives for SMBGET (65C02, cc65 calling convention).
;
; The CoGS card is not memory-mapped like the W5100. It exposes 16 slot
; registers at $C080 + slot*16 and a framed command protocol:
;
;   +0 DATA     write: next byte to the card / read: pop next byte
;   +1 STATUS   bit7 RX_AVAIL, bit6 TX_READY, bit4 LINK_UP, bit3 ERROR
;   +2 RXLO     queued card->host bytes (reading RXLO latches RXHI)
;   +3 RXHI
;   +4 CONTROL  write-only pulses: $08 flush host->card + reset parser,
;               $04 flush card->host
;   +5 ID       always $A2
;   +6 VERSION  nonzero
;   +A SYNC     increments when a parser reset completes
;
; Every DATA access uses abs,X addressing ("lda DATA,x" / "sta DATA,x"),
; which is the access pattern the CoGS technical reference documents as
; safe. A DATA read with nothing queued returns $00 and loses nothing, but
; it also returns a wrong byte, so every read is gated on STATUS RX_AVAIL.
;

        .export         _cogs_set_slot
        .export         _cogs_probe
        .export         _cogs_reg_rd
        .export         _cogs_reg_wr
        .export         _cogs_avail
        .export         _cogs_get
        .export         _cogs_getn
        .export         _cogs_skip
        .export         _cogs_put
        .export         _cogs_putn
        .export         _cogs_resync
        .export         _cogs_irq_off

        .import         popa, popax
        .importzp       ptr1, ptr2, tmp1, tmp2

REG_DATA        = $C080
REG_STATUS      = $C081
REG_RXLO        = $C082
REG_RXHI        = $C083
REG_CONTROL     = $C084
REG_ID          = $C085
REG_VERSION     = $C086
REG_SYNC        = $C08A

ST_RX_AVAIL     = $80
ST_TX_READY     = $40

        .bss
slotx:  .res    1               ; slot * 16
putmid: .res    1               ; middle byte of the TX wait counter

        .code

; void __fastcall__ cogs_set_slot(unsigned char slot)
_cogs_set_slot:
        asl
        asl
        asl
        asl
        sta     slotx
        rts

; unsigned char __fastcall__ cogs_probe(unsigned char slot)
; 1 if ID reads $A2, VERSION is nonzero and STATUS is not $FF.
; Reads only the ID, VERSION and STATUS registers; never DATA.
_cogs_probe:
        asl
        asl
        asl
        asl
        tax
        lda     REG_ID,x
        cmp     #$A2
        bne     @no
        lda     REG_VERSION,x
        beq     @no
        lda     REG_STATUS,x
        cmp     #$FF
        beq     @no
        lda     #1
        ldx     #0
        rts
@no:    lda     #0
        tax
        rts

; unsigned char __fastcall__ cogs_reg_rd(unsigned char reg)
; Not for DATA (use cogs_get).
_cogs_reg_rd:
        clc
        adc     slotx
        tax
        lda     REG_DATA,x
        ldx     #0
        rts

; void __fastcall__ cogs_reg_wr(unsigned char reg, unsigned char val)
_cogs_reg_wr:
        sta     tmp1            ; val
        jsr     popa            ; reg
        clc
        adc     slotx
        tax
        lda     tmp1
        sta     REG_DATA,x
        rts

; unsigned int cogs_avail(void)
; Number of card->host bytes queued and safe to pop.
_cogs_avail:
        ldx     slotx
        lda     REG_RXLO,x      ; latches RXHI
        pha
        lda     REG_RXHI,x
        tax
        pla
        rts

; int cogs_get(void)
; Wait for one byte (bounded) and pop it. Returns 0..255, or -1 on timeout.
_cogs_get:
        ldx     slotx
        lda     #0
        sta     tmp1
        sta     tmp2
        ldy     #$04            ; 4 * 65536 polls, about 3 s at 1 MHz
@w:     lda     REG_STATUS,x
        bmi     @rd
        inc     tmp1
        bne     @w
        inc     tmp2
        bne     @w
        dey
        bne     @w
        lda     #$FF
        tax
        rts
@rd:    lda     REG_DATA,x
        ldx     #0
        rts

; unsigned char __fastcall__ cogs_getn(unsigned char *dst, unsigned char n)
; Pop n bytes into dst. Each byte waits for RX_AVAIL like cogs_get does:
; reading DATA back to back on the strength of the RXLO/RXHI count alone
; is the suspect in garbled replies seen on real hardware.
; Returns 0 on success, 1 if a byte never arrived.
_cogs_getn:
        sta     ptr2            ; n
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        ldx     slotx
        ldy     #0
        cpy     ptr2
        beq     @ok
@byte:  lda     REG_STATUS,x
        bpl     @slow
@rd:    lda     REG_DATA,x
        sta     (ptr1),y
        iny
        cpy     ptr2
        bne     @byte
@ok:    lda     #0
        tax
        rts
@slow:  lda     #0              ; bounded wait, about 1 s at 1 MHz
        sta     tmp1
        sta     tmp2
@w:     lda     REG_STATUS,x
        bmi     @rd
        inc     tmp1
        bne     @w
        inc     tmp2
        bne     @w
        lda     #1
        ldx     #0
        rts

; unsigned char __fastcall__ cogs_skip(unsigned int n)
; Pop and discard n bytes, polling STATUS for each (the bytes may still
; be arriving). Returns 0 on success, 1 on timeout.
_cogs_skip:
        sta     ptr2
        stx     ptr2+1
@next:  lda     ptr2
        ora     ptr2+1
        beq     @ok
        jsr     _cogs_get
        cpx     #$FF
        beq     @to
        lda     ptr2
        bne     :+
        dec     ptr2+1
:       dec     ptr2
        jmp     @next
@ok:    lda     #0
        tax
        rts
@to:    lda     #1
        ldx     #0
        rts

; unsigned char __fastcall__ cogs_put(unsigned char b)
; Wait (bounded) for TX_READY and write one byte. 0 ok, 1 timeout.
_cogs_put:
        sta     tmp2            ; byte to send
        ldx     slotx
; put_x: byte in tmp2, slot offset in X. Clobbers A, Y, tmp1, putmid.
put_x:  lda     #0
        sta     tmp1
        sta     putmid
        ldy     #$04            ; 4 * 65536 polls, about 3 s at 1 MHz
@w:     lda     REG_STATUS,x
        and     #ST_TX_READY
        bne     @wr
        inc     tmp1
        bne     @w
        inc     putmid
        bne     @w
        dey
        bne     @w
        lda     #1
        ldx     #0
        rts
@wr:    lda     tmp2
        sta     REG_DATA,x
        lda     #0
        tax
        rts

; unsigned char __fastcall__ cogs_putn(const unsigned char *src, unsigned int n)
; Write n bytes, each gated on TX_READY. 0 ok, 1 timeout.
_cogs_putn:
        sta     ptr2
        stx     ptr2+1
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        ldx     slotx
@next:  lda     ptr2
        ora     ptr2+1
        beq     @ok
        lda     REG_STATUS,x    ; fast path: room for the byte already
        and     #ST_TX_READY
        beq     @slow
        lda     (ptr1)
        sta     REG_DATA,x
        bra     @adv
@slow:  lda     (ptr1)
        sta     tmp2
        jsr     put_x
        ldx     slotx
        cmp     #0
        bne     @to
@adv:   inc     ptr1
        bne     :+
        inc     ptr1+1
:       lda     ptr2
        bne     :+
        dec     ptr2+1
:       dec     ptr2
        jmp     @next
@ok:    lda     #0
        tax
        rts
@to:    lda     #1
        ldx     #0
        rts

; unsigned char cogs_irq_off(void)
; Mask IRQs. Returns 1 if they were enabled (I flag clear), else 0. A card
; driver's interrupt handler could otherwise pop our frames from the FIFO.
_cogs_irq_off:
        php
        pla
        sei
        and     #$04
        eor     #$04
        lsr
        lsr
        ldx     #0
        rts

; unsigned char cogs_resync(void)
; Flush host->card FIFO and reset the parser ($08), flush card->host ($04),
; then wait for SYNC to move. 0 ok, 1 SYNC never moved.
_cogs_resync:
        ldx     slotx
        lda     REG_SYNC,x
        sta     tmp2
        lda     #$08
        sta     REG_CONTROL,x
        lda     #$04
        sta     REG_CONTROL,x
        lda     #0
        sta     tmp1
        ldy     #0
@w:     lda     REG_SYNC,x
        cmp     tmp2
        bne     @ok
        inc     tmp1
        bne     @w
        iny
        bne     @w
        lda     #1
        ldx     #0
        rts
@ok:    lda     #0
        tax
        rts
