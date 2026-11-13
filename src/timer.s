; Millisecond counter for IP65 and the NIC overlays.
; VBL edges only: about 16 ms each, so the count steps by 16.
; Does not call WAIT and does not touch $C080 or $C0C0.
;
; 65C02. Exported under both the assembly name and the cc65 C name.

        .export         timer_init
        .export         _timer_init
        .export         timer_read
        .export         _timer_read
        .export         timer_jiffy
        .export         _timer_jiffy
        .export         timer_seconds

RDVBLBAR = $C019

        .bss
current_time:   .res    2
last_vbl:       .res    1

        .code

timer_init:
_timer_init:
        lda     #0
        sta     current_time
        sta     current_time+1
        lda     RDVBLBAR
        sta     last_vbl
        rts

timer_read:
_timer_read:
timer_jiffy:
_timer_jiffy:
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

timer_seconds:
        lda     #0
        rts
