;
; Copy the Applesoft keyword table out of the ROM: void bas_rom_table(uint8_t *dst), 512 bytes
; from $D0D0 (token $80 "END" first; each keyword's last character has bit 7 set).
; The language-card state is saved and restored around the ROM read.
;
        .export         _bas_rom_table
        .importzp       ptr1

RDLCBNK2        = $C011
RDLCRAM         = $C012
ROMRD           = $C082                 ; read ROM, write-protect RAM
LCB2RD          = $C080                 ; read LC bank 2 RAM, write-protect
LCB1RD          = $C088                 ; read LC bank 1 RAM, write-protect
TABLE           = $D0D0

        .bss
svram:  .res    1
svbnk:  .res    1

        .code

_bas_rom_table:
        sta     ptr1
        stx     ptr1+1
        php
        sei
        lda     RDLCRAM
        sta     svram
        lda     RDLCBNK2
        sta     svbnk
        bit     ROMRD
        ldy     #0
@p1:    lda     TABLE,y
        sta     (ptr1),y
        iny
        bne     @p1
        inc     ptr1+1
@p2:    lda     TABLE+$100,y
        sta     (ptr1),y
        iny
        bne     @p2
        lda     svram                   ; restore: ROM stays selected unless LC RAM was readable
        bpl     @done
        lda     svbnk
        bpl     @b1
        bit     LCB2RD
        bra     @done
@b1:    bit     LCB1RD
@done:  plp
        rts
