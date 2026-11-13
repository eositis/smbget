; First six bytes of the language-card image at $D400.
; The main program calls through these while bank 2 is switched in.
        .import         _names_host_to_prodos
        .import         _names_prodos_to_host
        .import         _names_type_str
        .import         _copy_tree
        .segment        "CODE"
        .addr           _names_host_to_prodos
        .addr           _names_prodos_to_host
        .addr           _names_type_str
        .addr           _copy_tree
