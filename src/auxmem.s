;
; Aux-bank block copy for 128K IIe/IIc.
; Writes use RAMWRT from MAIN. Reads run in zp $50: RAMRD switches
; $0200-$BFFF, so a stub at $0300 fetches opcodes from aux and BRKs.
; 80-col firmware owns aux $0400-$07FF; scrollback $0800-$2FFF;
; SMBGET: pane listings and SMB buffers use $4000-$BFFF.
;
        .export         _aux_present
        .export         _aux_write
        .export         _aux_read
        .export         _aux_abs_write
        .export         _aux_abs_read
        .export         _aux_mainbank
        .import         popax
        .include        "zeropage.inc"

AUX_BASE        = $4000
CLR_RAMRD       = $C002
SET_RAMRD       = $C003
CLR_RAMWRT      = $C004
SET_RAMWRT      = $C005

        .bss
zpsv:   .res    48

        .code

_aux_present:
        php
        sei
        lda     $4000
        sta     tmp1
        sta     SET_RAMWRT
        eor     #$FF
        sta     $4000
        sta     CLR_RAMWRT
        lda     $4000
        cmp     tmp1
        bne     @no
        lda     #1
        ldx     #0
        plp
        rts
@no:    lda     tmp1
        sta     $4000
        lda     #0
        tax
        plp
        rts

_aux_mainbank:
        sta     CLR_RAMRD
        sta     CLR_RAMWRT
        sta     $C054
        rts

_aux_write:
        sta     tmp1
        stx     tmp2
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        jsr     popax
        jsr     set_auxptr
        lda     tmp1
        ora     tmp2
        beq     @out
        php
        sei
        sta     CLR_RAMRD
        sta     SET_RAMWRT
        jsr     copy_to_aux
        sta     CLR_RAMWRT
        plp
@out:   rts

_aux_read:
        sta     tmp1
        stx     tmp2
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        jsr     popax
        jsr     set_auxptr
        lda     tmp1
        ora     tmp2
        beq     @rdz
        ldx     #0
@sv:    lda     $50,x
        sta     zpsv,x
        inx
        cpx     #stub_len
        bcc     @sv
        ldx     #0
@inst:  lda     stub_img,x
        sta     $50,x
        inx
        cpx     #stub_len
        bcc     @inst
        php
        sei
        jsr     $50
        sta     CLR_RAMRD
        sta     CLR_RAMWRT
        sta     $C054
        plp
        ldx     #0
@rs:    lda     zpsv,x
        sta     $50,x
        inx
        cpx     #stub_len
        bcc     @rs
@rdz:   rts

set_auxptr:
        clc
        adc     #<AUX_BASE
        sta     ptr2
        txa
        adc     #>AUX_BASE
        sta     ptr2+1
        rts

set_auxptr_abs:
        sta     ptr2
        stx     ptr2+1
        rts

_aux_abs_write:
        sta     tmp1
        stx     tmp2
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        jsr     popax
        jsr     set_auxptr_abs
        lda     tmp1
        ora     tmp2
        beq     @aw
        php
        sei
        sta     CLR_RAMRD
        sta     SET_RAMWRT
        jsr     copy_to_aux
        sta     CLR_RAMWRT
        plp
@aw:    rts

_aux_abs_read:
        sta     tmp1
        stx     tmp2
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        jsr     popax
        jsr     set_auxptr_abs
        lda     tmp1
        ora     tmp2
        beq     @arz
        ldx     #0
@asv:   lda     $50,x
        sta     zpsv,x
        inx
        cpx     #stub_len
        bcc     @asv
        ldx     #0
@ain:   lda     stub_img,x
        sta     $50,x
        inx
        cpx     #stub_len
        bcc     @ain
        php
        sei
        jsr     $50
        sta     CLR_RAMRD
        sta     CLR_RAMWRT
        sta     $C054
        plp
        ldx     #0
@ars:   lda     zpsv,x
        sta     $50,x
        inx
        cpx     #stub_len
        bcc     @ars
@arz:   rts

copy_to_aux:
        ldy     #0
        ldx     tmp2
        beq     @part
@page:  lda     (ptr1),y
        sta     (ptr2),y
        iny
        bne     @page
        inc     ptr1+1
        inc     ptr2+1
        dex
        bne     @page
@part:  lda     tmp1
        beq     @done
@pl:    lda     (ptr1),y
        sta     (ptr2),y
        iny
        cpy     tmp1
        bne     @pl
@done:  rts

        .rodata
stub_img:
        sta     CLR_RAMWRT
        sta     SET_RAMRD
        ldy     #0
        ldx     tmp2
        beq     rpart
rpage:  lda     (ptr2),y
        sta     (ptr1),y
        iny
        bne     rpage
        inc     ptr1+1
        inc     ptr2+1
        dex
        bne     rpage
rpart:  lda     tmp1
        beq     rdone
rpl:    lda     (ptr2),y
        sta     (ptr1),y
        iny
        cpy     tmp1
        bne     rpl
rdone:  sta     CLR_RAMRD
        rts
stub_len = * - stub_img
.assert stub_len <= 48, error, "aux stub exceeds zpsv"
