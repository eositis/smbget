;
; Call the name tables in language-card bank 2 ($D400) and copy that image
; in at startup. Bank 2 RAM at $D000-$DFFF hides ROM, including the IRQ
; vector, so the calls run with interrupts off. $C081 writes bank 2 while
; ROM stays readable; $C083 executes it; $C082 puts ROM back.
;
        .export         _names_host_to_prodos
        .export         _names_prodos_to_host
        .export         _names_type_str
        .export         _copy_tree
        .export         _lc_move
        .export         _lc_ok
        .export         _lcstub_ensure_dir
        .export         _lcstub_next_child
        .export         _lcstub_copy_file_paths
        .import         _ensure_dir
        .import         _next_child
        .import         _copy_file_paths
        .import         decsp3
        .import         decsp6
        .import         popax
        .include        "zeropage.inc"

LC_WRITE        = $C081
LC_ROM          = $C082
LC_READ         = $C083
VEC             = $D400

        .code

; Y = 0, 2 or 4. The C argument is already in A/X and on the software stack.
.proc call_lc
        php
        sei
        pha
        phx
        lda     LC_READ
        lda     LC_READ
        lda     VEC,y
        sta     go+1
        iny
        lda     VEC,y
        sta     go+2
        plx
        pla
        jsr     go
        pha
        lda     LC_ROM
        pla
        plp
        rts
go:     jmp     $FFFF
.endproc

.proc _names_host_to_prodos
        ldy     #0
        jmp     call_lc
.endproc

.proc _names_prodos_to_host
        ldy     #2
        jmp     call_lc
.endproc

.proc _names_type_str
        ldy     #4
        jmp     call_lc
.endproc

.proc _copy_tree
        ldy     #6
        jmp     call_lc
.endproc

; The folder walk runs from bank 2. These return to it after a main-RAM
; routine that needs ROM (MLI, conio) has finished.
.proc stub_back
        sei
        pha
        phx
        lda     LC_READ
        lda     LC_READ
        plx
        pla
        rts
.endproc

.proc _lcstub_ensure_dir
        lda     LC_ROM
        cli
        jsr     _ensure_dir
        jmp     stub_back
.endproc

.proc _lcstub_next_child
        lda     LC_ROM
        cli
        jsr     _next_child
        jmp     stub_back
.endproc

.proc _lcstub_copy_file_paths
        lda     LC_ROM
        cli
        jsr     _copy_file_paths
        jmp     stub_back
.endproc

; void __fastcall__ lc_move(unsigned dest, const unsigned char *src, unsigned n);
; ROM stays mapped. Writes land in bank 2.
.proc _lc_move
        php
        pha
        phx
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        jsr     popax
        sta     ptr2
        stx     ptr2+1
        plx
        stx     tmp2
        pla
        sta     tmp1
        sei
        lda     LC_WRITE
        lda     LC_WRITE
        ldy     #0
        ldx     tmp2
        beq     part
page:   lda     (ptr1),y
        sta     (ptr2),y
        iny
        bne     page
        inc     ptr1+1
        inc     ptr2+1
        dex
        bne     page
part:   lda     tmp1
        beq     done
byte:   lda     (ptr1),y
        sta     (ptr2),y
        iny
        cpy     tmp1
        bne     byte
done:   lda     LC_ROM
        plp
        rts
.endproc

; Nonzero once the image's first vector points into bank 2.
.proc _lc_ok
        php
        sei
        lda     LC_READ
        lda     LC_READ
        lda     VEC+1
        cmp     #$D4
        lda     LC_ROM
        lda     #0
        rol
        plp
        ldx     #0
        rts
.endproc

; Keep the stack helpers the BASIC overlay calls, or that overlay no longer fits.
.byte <decsp3, >decsp3, <decsp6, >decsp6
