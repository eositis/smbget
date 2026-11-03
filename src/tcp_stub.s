; IP65 ip_process calls these. Hardware TCP owns the socket after
; w5100_config(), so the software TCP engine is not linked.
        .export tcp_init
        .export tcp_process

        .code

tcp_init:
tcp_process:
        clc
        rts
