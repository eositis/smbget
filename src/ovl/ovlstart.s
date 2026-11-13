; First bytes of SMBGET.OVL: a jump to the C dispatcher and a signature the loader checks.
        .import         _ovl_entry
        .segment        "OVLSTART"
        jmp     _ovl_entry
        .byte   "SMBOVL1"
