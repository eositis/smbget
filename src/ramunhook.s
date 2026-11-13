;
; Drop ProDOS /RAM (slot 3, drive 2) before anything touches auxiliary
; memory. The driver vector is pointed at the "no device" routine, and
; the device list loses every unit whose high nibble is $B0. /RAM5 on a
; IIgs is a different unit and is left alone.
;
        .export         _pd_unhook_ram

DEVTBL          = $BF10
RAMDRV          = $BF26
DEVCNT          = $BF31
DEVLST          = $BF32

        .code

.proc _pd_unhook_ram
        lda     DEVTBL
        sta     RAMDRV
        lda     DEVTBL+1
        sta     RAMDRV+1
        lda     DEVCNT
        cmp     #$FF                    ; already empty
        beq     done
        sta     bound
        ldx     #0
        ldy     #0
scan:   lda     DEVLST,x
        pha
        and     #$F0
        cmp     #$B0                    ; slot 3, drive 2
        beq     drop
        pla
        sta     DEVLST,y
        iny
        bra     next
drop:   pla
next:   cpx     bound
        beq     store
        inx
        bra     scan
store:  dey
        sty     DEVCNT
done:   rts
bound:  .byte   0
.endproc
