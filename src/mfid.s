; MegaFlash presence at $C0C0 (slot 4). Not the Uthernet window at $C0C4.
; Caller must already know this is a ROM 4/5 IIc.
;
; 65C02. BIT copies bit 7 of the operand into N and bit 6 into V.

        .export         _mf_present

cmdreg          = $C0C0
paramreg        = $C0C1
CMD_GETDEVINFO  = $10

        .code

; unsigned char mf_present(void)
; CMD_GETDEVINFO signature $88,$74. Two back-to-back reads of $C0C3 XOR
; to 0 on the Pico bus, so the ID toggle is not used.
_mf_present:
        lda     #CMD_GETDEVINFO
        sta     cmdreg
        ldx     #0
:       bit     cmdreg
        bpl     @idle
        dex
        bne     :-
        beq     @no
@idle:  bit     cmdreg
        bvs     @no
        lda     paramreg
        cmp     #$88
        bne     @no
        lda     paramreg
        cmp     #$74
        bne     @no
        lda     #1
        ldx     #0
        rts
@no:    lda     #0
        tax
        rts
