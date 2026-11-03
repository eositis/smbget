; ProDOS MLI caller
;   unsigned char __fastcall__ pd_mli(unsigned char cmd, void *params);
; Returns 0 on success, else the MLI error code.

        .export _pd_mli
        .import popa

        .code

_pd_mli:
        sta     params
        stx     params+1
        jsr     popa
        sta     cmd
        jsr     $BF00
cmd:    .byte   0
params: .addr   0
        ldx     #0
        bcs     :+
        txa
:       rts
