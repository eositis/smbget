;
; The SMB message buffer. ip65's output_buffer (used only to build the DHCP packet, before any
; TCP connection exists) is the same memory.
;
        .export         output_buffer
        .export         _output_buffer
        .export         _smbpkt

        .bss

output_buffer:
_output_buffer:
_smbpkt:        .res    800
