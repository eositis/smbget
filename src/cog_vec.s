; CoGS overlay header. Must be the first CODE in the link.
; Offsets match struct nic in src/nic.h.

        .import _cog_probe
        .import _cog_init
        .import _cog_connect
        .import _cog_send
        .import _cog_recv
        .import _cog_close

        .segment "CODE"
        .byte "NIC1"
        .byte $00
        .byte $01
        .word 0
        .addr _cog_probe
        .addr _cog_init
        .addr _cog_connect
        .addr _cog_send
        .addr _cog_recv
        .addr _cog_close
