; Uthernet II overlay header. Must be the first CODE in the link.
; Offsets match struct nic in src/nic.h. load_size is patched at offset 6.

        .import _uth_probe
        .import _uth_init
        .import _uth_connect
        .import _uth_send
        .import _uth_recv
        .import _uth_close

        .segment "CODE"
        .byte "NIC1"
        .byte $00
        .byte $00
        .word 0
        .addr _uth_probe
        .addr _uth_init
        .addr _uth_connect
        .addr _uth_send
        .addr _uth_recv
        .addr _uth_close
