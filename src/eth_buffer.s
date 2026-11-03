;
; Smaller Ethernet frame buffers than stock ip65 (1518 each).
; RX: TCP window is patched to 800, so frames are <= 14+20+20+800 = 854.
; TX: net_tcp_send chunks at 512 bytes -> frames <= 566 (buffer 576); DHCP frame is ~342.
;
        .export         eth_inp
        .export         eth_outp
        .export         _eth_inp
        .export         _eth_outp

        .bss

eth_inp:
_eth_inp:       .res    880
eth_outp:
_eth_outp:      .res    400
