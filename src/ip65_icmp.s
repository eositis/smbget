;
; Drop ICMP (no ping replies): saves ~550 bytes of main memory.
;
        .export         icmp_init
        .export         icmp_process

        .code

icmp_init:
        rts

icmp_process:
        sec
        rts
