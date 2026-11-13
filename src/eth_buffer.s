;
; Smaller Ethernet frame buffers than stock ip65 (1518 each).
; RX: frames are <= 854. TX: DHCP frame is ~342, and a volume list of 14
; entries fits in the 352. The two buffers together also hold a page-aligned
; 1K ProDOS I/O buffer (see prodos.c). src/ovl/ovl.c assumes eth_outp is 352.
;
        .export         eth_inp
        .export         eth_outp
        .export         _eth_inp
        .export         _eth_outp

        .bss

eth_inp:
_eth_inp:       .res    880
eth_outp:
_eth_outp:      .res    352
