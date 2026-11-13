; Uthernet overlay clock. MegaFlash CMD_GETTIMER_MS when this is a
; IIc with MegaFlash, otherwise the same $C019 VBL count as the main
; program. The accelerator is not written: slot 4 stays fast, and the
; read uses the $C0C0 command register MegaFlash already serves at 4 MHz.
;
; The command returns 32 bits, little-endian. AX is the low 16. sreg
; is not written. 65C02. BIT copies bit 7 into N and bit 6 into V.

        .export         timer_init
        .export         _timer_init
        .export         timer_read
        .export         _timer_read
        .import         _mf_present

RDVBLBAR          = $C019
cmdreg            = $C0C0
paramreg          = $C0C1
CMD_RESETTIMER_MS = $42
CMD_GETTIMER_MS   = $43

        .bss
current_time:   .res    2
last_vbl:       .res    1
use_mf:         .res    1

        .code

timer_init:
_timer_init:
        lda     #0
        sta     current_time
        sta     current_time+1
        sta     use_mf
        lda     RDVBLBAR
        sta     last_vbl
        lda     $FBB3
        cmp     #6
        bne     @done
        lda     $FBC0
        cmp     #$E0
        beq     @done
        lda     $FBBF
        cmp     #4
        beq     @probe
        cmp     #5
        bne     @done
@probe: jsr     _mf_present
        cmp     #1
        bne     @done
        lda     #CMD_RESETTIMER_MS
        sta     cmdreg
        jsr     mf_wait
        bcs     @done
        lda     #1
        sta     use_mf
@done:  rts

timer_read:
_timer_read:
        lda     use_mf
        beq     vbl_read
        lda     #CMD_GETTIMER_MS
        sta     cmdreg
        jsr     mf_wait
        bcs     @zero
        lda     paramreg
        pha
        lda     paramreg
        pha
        lda     paramreg
        lda     paramreg
        pla
        tax
        pla
        rts
@zero:  lda     #0
        tax
        rts

vbl_read:
        lda     RDVBLBAR
        tax
        eor     last_vbl
        stx     last_vbl
        bpl     @ret
        txa
        bmi     @ret
        lda     current_time
        clc
        adc     #16
        sta     current_time
        bcc     @ret
        inc     current_time+1
@ret:   lda     current_time
        ldx     current_time+1
        rts

mf_wait:
        ldx     #0
:       bit     cmdreg
        bpl     @idle
        dex
        bne     :-
        sec
        rts
@idle:  bit     cmdreg
        bvs     @err
        clc
        rts
@err:   sec
        rts
