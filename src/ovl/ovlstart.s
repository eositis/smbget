; First bytes of SMBGET.OVL at $0800: a jump to the C dispatcher and a signature the loader checks.
        .import         _ovl_entry
        .segment        "OVLSTART"
        jmp     _ovl_entry
        .byte   "SMBOVL1"
