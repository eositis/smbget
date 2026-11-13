; Bytes ahead of the ethernet buffers so eth_inp's page offset stays
; at least $30. A 1K buffer aligned up from a lower offset would run
; past the end of those buffers.
        .segment "BSS"
        .res 18
