;
; Slim ip65_strerror: only codes SMBGET can hit.
;
        .include        "error.inc"

        .export         ip65_strerror

        .code

ip65_strerror:
        cmp     #IP65_ERROR_TIMEOUT_ON_RECEIVE
        bne     :+
        lda     #<str_timeout
        ldx     #>str_timeout
        rts
:       cmp     #IP65_ERROR_TRANSMIT_FAILED
        bne     :+
        lda     #<str_send
        ldx     #>str_send
        rts
:       cmp     #IP65_ERROR_DEVICE_FAILURE
        bne     :+
        lda     #<str_nodev
        ldx     #>str_nodev
        rts
:       cmp     #IP65_ERROR_ABORTED_BY_USER
        bne     :+
        lda     #<str_abort
        ldx     #>str_abort
        rts
:       cmp     #IP65_ERROR_CONNECTION_RESET_BY_PEER
        bne     :+
        lda     #<str_reset
        ldx     #>str_reset
        rts
:       cmp     #IP65_ERROR_CONNECTION_CLOSED
        bne     :+
        lda     #<str_closed
        ldx     #>str_closed
        rts
:       lda     #<str_unknown
        ldx     #>str_unknown
        rts

        .rodata

str_timeout:
        .byte   "Timeout",0
str_send:
        .byte   "Send failed",0
str_nodev:
        .byte   "No device found",0
str_abort:
        .byte   "User abort",0
str_reset:
        .byte   "Connection reset by peer",0
str_closed:
        .byte   "Connection closed",0
str_unknown:
        .byte   "Unknown error",0
