; samout.s - SAM's real-time output loop for the C64 (6502, ca65 syntax).
;
; void __fastcall__ SamPlay(unsigned char nframes);
;
; Plays the frame tables built by render.c (pitches, frequency1..3,
; amplitude1..3, sampledConsonantFlag) through the SID volume register,
; following the control flow of render.c's output loop step for step.
; Timing matches the original 6502 SAM (the C port's timetable):
;   voiced tick          145 cycles  (timetable 162/50 samples at 22050 Hz)
;   unvoiced sample bit  ~53.6 cycles on average (60)
;   voiced sample bit    ~48 cycles (54)
; Every voiced-tick path is padded to the same length, so pitch and formant
; frequencies come out exactly as in the C reference. A frame change adds
; one longer tick every ~10 ms, as the original did.
;
; IRQs are off and the screen is blanked while speaking (bad lines would
; steal cycles). Build with -D SAM_TEST for the cycle-checking harness,
; which has no VIC-II to wait for.

        .export _SamPlay, _SamPlayTables, _samTables
        .import _pitches, _frequency1, _frequency2, _frequency3
        .import _amplitude1, _amplitude2, _amplitude3, _sampledConsonantFlag
        .import _speed, _sampleTable, _tab48426
        .import SINTAB, RECTAB, NIBTAB

SIDVOL  = $D418
VICCR1  = $D011

; hot variables in zero page (C64: $F9-$FE are free while the KERNAL's
; RS-232 is unused; saved and restored anyway)
ph1     = $F9           ; formant phases
ph2     = $FA
ph3     = $FB
spd     = $FC           ; SAM's speedcounter (ticks left in this frame)
cnt44   = $FD           ; SAM's mem44: ticks left in this glottal period
cnt38   = $FE           ; SAM's mem38: ticks left before a voiced-sample slice

        .segment "BSS"
_samTables: .res 16     ; see SamPlayTables
m39:    .res 1          ; flag used by this tick (sampledConsonantFlag)
m39n:   .res 1          ; flag of the current frame, taken over at the next tick:
                        ; render.c reads it at the top of its loop, so on a frame
                        ; change the old frame's flag still decides that tick
m48:    .res 1          ; frames left
m66:    .res 1          ; read position in voiced samples
tmp:    .res 1
bits:   .res 1
bitcnt: .res 1
vcnt:   .res 1
zeronib:.res 1
savex:  .res 1
savezp: .res 6
saved011: .res 1

        .segment "CODE"

; ---------------------------------------------------------------- entry

; void __fastcall__ SamPlayTables(unsigned char nframes);
; Like SamPlay, but reads the frame tables from the 8 pointers in
; samTables (pitches, frequency1..3, amplitude1..3, flags), so queued
; phrases can be played without copying them back (c64/main.c).
_SamPlayTables:
        pha                     ; keep nframes (A) while patching
        lda _samTables+0
        sta tbl0+1
        lda _samTables+1
        sta tbl0+2
        lda _samTables+0
        sta tbl8+1
        lda _samTables+1
        sta tbl8+2
        lda _samTables+0
        sta tbl9+1
        lda _samTables+1
        sta tbl9+2
        lda _samTables+2
        sta tbl4+1
        lda _samTables+3
        sta tbl4+2
        lda _samTables+4
        sta tbl5+1
        lda _samTables+5
        sta tbl5+2
        lda _samTables+6
        sta tbl6+1
        lda _samTables+7
        sta tbl6+2
        lda _samTables+8
        sta tbl1+1
        lda _samTables+9
        sta tbl1+2
        lda _samTables+10
        sta tbl2+1
        lda _samTables+11
        sta tbl2+2
        lda _samTables+12
        sta tbl3+1
        lda _samTables+13
        sta tbl3+2
        lda _samTables+14
        sta tbl7+1
        lda _samTables+15
        sta tbl7+2
        pla                     ; nframes
        ; fall through into SamPlay

_SamPlay:
        sta m48
        php
        sei
        ldx #5
@save:  lda ph1,x
        sta savezp,x
        dex
        bpl @save
.ifndef SAM_TEST
        lda VICCR1
        sta saved011
        and #$EF                ; blank the screen: no bad lines
        sta VICCR1
@w1:    bit VICCR1              ; wait for a new frame so it takes effect
        bpl @w1
@w2:    bit VICCR1
        bmi @w2
.endif
        ; initial state, as render.c before its loop
        lda #0
        sta ph1
        sta ph2
        sta ph3
        sta m66
        lda #72                 ; speedcounter = 72 ("sam standard speed")
        sta spd
tbl0:
        lda _pitches
        sta cnt44
        lsr
        lsr
        sta tmp
        lda cnt44
        sec
        sbc tmp
        sta cnt38               ; mem38 = A - (A>>2)
        ldx #0                  ; X = SAM's Y (frame index) from here on
        jsr setframe
        jmp loop

; ---------------------------------------------------------------- exit
done:
        ldx #5
@rest:  lda savezp,x
        sta ph1,x
        dex
        bpl @rest
.ifndef SAM_TEST
        lda saved011
        sta VICCR1
.endif
        plp
        rts

; ---------------------------------------------------------------- frame setup
; Patch the current frame's amplitude pages and frequencies into the tick
; code and load its flag. X = frame. (~70 cycles, once per frame)
setframe:
tbl1:
        lda _amplitude1,x
        and #$0F
        clc
        adc #>SINTAB
        sta s1+2
tbl2:
        lda _amplitude2,x
        and #$0F
        clc
        adc #>SINTAB
        sta s2+2
tbl3:
        lda _amplitude3,x
        and #$0F
        clc
        adc #>RECTAB
        sta s3+2
tbl4:
        lda _frequency1,x
        sta adv1+1
tbl5:
        lda _frequency2,x
        sta adv2+1
tbl6:
        lda _frequency3,x
        sta adv3+1
tbl7:
        lda _sampledConsonantFlag,x
        sta m39n
        rts

; ---------------------------------------------------------------- main loop
; One pass = one tick of SAM's loop. The SIDVOL write sits at a fixed offset
; from "loop", so the tick length is the length of the path back to "loop".
loop:
        lda m39n                ; 4   A = sampledConsonantFlag[Y]; mem39 = A
        sta m39                 ; 4
        and #$F8                ; 2   unvoiced sampled consonant?
        beq voiced              ; 3
        jmp unvoiced
voiced:
        ldy ph1                 ; 3
s1:     lda SINTAB,y            ; 4   page patched per frame: amplitude1
        ldy ph2                 ; 3
        clc                     ; 2
s2:     adc SINTAB,y            ; 4   amplitude2
        ldy ph3                 ; 3
        clc                     ; 2
s3:     adc RECTAB,y            ; 4   amplitude3
        tay                     ; 2
        lda NIBTAB,y            ; 4
        sta SIDVOL              ; 4   -> 48 cycles from loop
        dec spd                 ; 5
        bne p155                ; 3
        ; frame done: Y++, mem48--
        inx                     ; 2
        dec m48                 ; 6
        bne @more               ; 3
        jmp done
@more:  lda _speed              ; 4
        sta spd                 ; 3
        jsr setframe            ; ~76
        jmp p155c               ; 3   (padding is shortened on this path)

p155:                           ; 56 cycles so far on the common path
        ; padding: 35 cycles (16 NOPs + BIT zp) so every voiced tick is 145
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        bit $00                 ; -> 91
p155c:
        dec cnt44               ; 5   mem44--: end of the glottal period?
        beq newper              ; 2/3
        dec cnt38               ; 5   mem38--
        bne advA                ; 3/2
        lda m39                 ; 4   mem38 hit 0: voiced sampled consonant?
        beq advance             ; 3
        ; play a slice of the noise sample, then start a new glottal period
        ; (render.c: RenderSample, goto pos48159)
        jsr samplevoiced
        jmp newper
advA:   nop                     ; 6   balance with the mem38 == 0 path
        nop
        nop
advance:                        ; 91 + 21 = 112
        lda ph1                 ; 3
        clc                     ; 2
adv1:   adc #0                  ; 2   frequency1 of this frame (patched)
        sta ph1                 ; 3
        lda ph2                 ; 3
        clc                     ; 2
adv2:   adc #0                  ; 2   frequency2
        sta ph2                 ; 3
        lda ph3                 ; 3
        clc                     ; 2
adv3:   adc #0                  ; 2   frequency3
        sta ph3                 ; 3   -> 142
        jmp loop                ; 3   -> 145

newper:                         ; start of a glottal period (pos48159); 91 + 8 = 99
tbl8:
        lda _pitches,x          ; 4
        sta cnt44               ; 3
        lsr                     ; 2
        lsr                     ; 2
        sta tmp                 ; 4
        lda cnt44               ; 3
        sec                     ; 2
        sbc tmp                 ; 4
        sta cnt38               ; 3   mem38 = A - (A>>2)
        lda #0                  ; 2
        sta ph1                 ; 3
        sta ph2                 ; 3
        sta ph3                 ; 3   -> 137
        nop                     ; 2
        bit $00                 ; 3   -> 142
        jmp loop                ; 3   -> 145

; ---------------------------------------------------------------- noise samples
; Unvoiced sampled consonant (render.c RenderSample, unvoiced branch): play
; the whole 1-bit sample from offset (flag & $F8) ^ $FF to the end of its
; row, one output per bit, then skip two frames.
unvoiced:
        stx savex
        lda m39
        and #$07
        tay
        dey                     ; row = (flag & 7) - 1
        lda _tab48426,y
        and #$0F
        sta zeronib             ; output for 0 bits; 1 bits output 5
        tya
        clc
        adc #>_sampleTable
        sta ld1+2
        lda #<_sampleTable
        sta ld1+1
        lda m39
        and #$F8
        eor #$FF
        tay                     ; start offset
ubyte:
ld1:    lda $FFFF,y             ; 4/5
        sta bits                ; 4
        lda #8                  ; 2
        sta bitcnt              ; 4
@bit:   asl bits                ; 6
        bcs @one                ; 2/3
        lda zeronib             ; 4
        jmp @out                ; 3   zero: 15
@one:   lda #5                  ; 2
        nop                     ; 2
        nop                     ; 2   one: 15
@out:   sta SIDVOL              ; 4
        jsr delay_u             ; pad
        dec bitcnt              ; 6
        bne @bit                ; 3
        iny                     ; 2
        bne ubyte               ; 3
        ; after the sample: Y += 2, mem48 -= 2 (wraps like the C code)
        ldx savex
        inx
        inx
        lda m48
        sec
        sbc #2
        sta m48
        bne @more
        jmp done
@more:  lda _speed
        sta spd
        lda #1                  ; RenderSample leaves mem44 = 1
        sta cnt44
        jsr setframe
        jmp p155c               ; -> mem44 hits 0 -> new period

; Voiced sampled consonant slice (RenderSample, voiced branch): (pitch>>4)+1
; bytes from the running position m66; 1 bits output 10, 0 bits output 6.
samplevoiced:
        stx savex
        lda m39
        and #$07
        tay
        dey
        tya
        clc
        adc #>_sampleTable
        sta ld2+2
        lda #<_sampleTable
        sta ld2+1
tbl9:
        lda _pitches,x
        lsr
        lsr
        lsr
        lsr
        eor #$FF
        sta vcnt                ; counts up to 0: (pitch>>4)+1 bytes
        ldy m66
vbyte:
ld2:    lda $FFFF,y
        sta bits
        lda #8
        sta bitcnt
@bit:   asl bits                ; 6
        bcs @one                ; 2/3
        lda #6                  ; 2
        jmp @out                ; 3   zero: 13
@one:   lda #10                 ; 2
        nop                     ; 2   one: 13
@out:   sta SIDVOL              ; 4
        nop                     ; 19 cycles: per bit 13 + 4 + 19 + 9 = 45, plus 24
        nop                     ; per byte -> 384 per 8 bits (48 avg; the
        nop                     ; original's 54/50 samples = 48.3 cycles)
        nop
        nop
        nop
        nop
        nop
        bit $00
        dec bitcnt              ; 6
        bne @bit                ; 3
        iny
        inc vcnt
        bne vbyte
        sty m66
        ldx savex
        rts

; Padding for the unvoiced sample loop: JSR + RTS = 12, plus 11 -> 23 cycles.
; Per bit 15 + 4 + 23 + 9 = 51, plus 20 per byte -> 428 per 8 bits (53.5 avg,
; the original's 60/50 samples at 22050 Hz = 53.6 cycles).
delay_u:
        nop
        nop
        nop
        nop
        bit $00
        rts
