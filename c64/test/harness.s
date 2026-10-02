; py65 driver for SamPlayTables (tables via samTables pointers)
        .export _pitches, _frequency1, _frequency2, _frequency3
        .export _amplitude1, _amplitude2, _amplitude3, _sampledConsonantFlag
        .export _speed, _sampleTable, _tab48426, entry, entry2, slot
        .import _SamPlay, _SamPlayTables
        .segment "CODE"
entry:  jsr _SamPlay
        brk
entry2: jsr _SamPlayTables
        brk
        .segment "BSS"
_pitches:               .res 256
_frequency1:            .res 256
_frequency2:            .res 256
_frequency3:            .res 256
_amplitude1:            .res 256
_amplitude2:            .res 256
_amplitude3:            .res 256
_sampledConsonantFlag:  .res 256
_speed:                 .res 1
_tab48426:              .res 5
_sampleTable:           .res $500
slot:                   .res $800
