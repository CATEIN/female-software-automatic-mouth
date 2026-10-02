; render_asm.s - SAM's frame builder in 6502 assembly (C64 / 6502 builds).
;
; unsigned char __fastcall__ RenderFrames(unsigned char female);
;
; Does what render.c's Render() does before its output loop:
;   CREATE FRAMES       copy each phoneme's targets into its frames, with
;                       AddInflection for "." and "?"
;   CREATE TRANSITIONS  blend formants, amplitudes and pitch between phonemes
; and returns the number of frames (render.c's mem48). The pitch contour,
; female pitch and amplitude rescale that follow are in render_fast.s.
;
; It follows the C step for step, including SAM's quirks: AddInflection
; leaves SAM's A register changed and the next "A == 2" test sees it,
; counters wrap at 256, and the pitch blend's start position is overwritten
; by phase3 as in the C. tools/check_c64.py frames compares the result with
; the native build.

        .export _RenderFrames
        .import _phonemeIndexOutput, _stressOutput, _phonemeLengthOutput
        .import _pitch
        .import _pitches, _frequency1, _frequency2, _frequency3
        .import _amplitude1, _amplitude2, _amplitude3, _sampledConsonantFlag
        .import _freq1data, _freq2data, _freq3data, _femaleFreq
        .import _ampl1data, _ampl2data, _ampl3data, _sampledConsonantFlags
        .import _tab47492, _blendRank, _outBlendLength, _inBlendLength
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

; zero-page scratch (cc65's, free inside a leaf routine)
m56     = tmp1          ; SAM's mem56 (inner blend accumulator)
m51     = tmp2          ; mem51: remainder step
m40     = tmp3          ; mem40: frames to blend over
m48     = tmp4          ; Render's local mem48

        .segment "BSS"
rA:     .res 1          ; SAM's A register (global A)
rX:     .res 1          ; X
rY:     .res 1          ; Y
m44:    .res 1          ; mem44: phoneme index
m47:    .res 1          ; mem47: table selector 168..174
m49:    .res 1          ; mem49: frame position
m50:    .res 1          ; mem50: sign of the blend step
m53:    .res 1          ; mem53: per-frame step
m36:    .res 1
m37:    .res 1
m38:    .res 1
ph1:    .res 1          ; Render's phase1..3
ph2:    .res 1
ph3:    .res 1
spc:    .res 1          ; speedcounter
dir:    .res 1          ; AddInflection's direction (1 or 255)
aph:    .res 1          ; AddInflection's phase1
dvd:    .res 1          ; division scratch

        .segment "RODATA"
; frame tables selected by mem47 = 168..174
tablo:  .lobytes _pitches, _frequency1, _frequency2, _frequency3, _amplitude1, _amplitude2, _amplitude3
tabhi:  .hibytes _pitches, _frequency1, _frequency2, _frequency3, _amplitude1, _amplitude2, _amplitude3

        .segment "CODE"

_RenderFrames:
        ; formant sources: female tables or SAM's own
        tax
        beq @male
        lda #<_femaleFreq
        sta ptr2
        lda #>_femaleFreq
        sta ptr2+1
        lda #<(_femaleFreq+80)
        sta ptr3
        lda #>(_femaleFreq+80)
        sta ptr3+1
        lda #<(_femaleFreq+160)
        sta ptr4
        lda #>(_femaleFreq+160)
        sta ptr4+1
        jmp createFrames
@male:  lda #<_freq1data
        sta ptr2
        lda #>_freq1data
        sta ptr2+1
        lda #<_freq2data
        sta ptr3
        lda #>_freq2data
        sta ptr3+1
        lda #<_freq3data
        sta ptr4
        lda #>_freq3data
        sta ptr4+1

; ---------------------------------------------------------------- CREATE FRAMES
createFrames:
        lda #0
        sta rA
        sta rX
        sta m44
        sta ph1
@phoneme:
        ldy m44
        sty rY                  ; Y = mem44
        lda _phonemeIndexOutput,y
        sta rA                  ; A = phonemeIndexOutput[mem44]; mem56 = A
        sta m56
        cmp #255
        bne @notEnd
        jmp transitions
@notEnd:
        cmp #1                  ; "." : falling inflection
        bne @notPeriod
        lda #1
        sta rA
        sta dir
        jsr addInflection
@notPeriod:
        lda rA                  ; (A as AddInflection left it)
        cmp #2                  ; "?" : rising inflection
        bne @notQuestion
        lda #255
        sta dir
        jsr addInflection
@notQuestion:
        ldy rY
        ldx _stressOutput,y
        lda _tab47492+1,x
        sta ph1                 ; phase1 = tab47492[stress + 1]
        lda _phonemeLengthOutput,y
        sta ph2                 ; phase2 = frames for this phoneme
        ldy m56
        sty rY                  ; Y = phoneme
        ldx rX                  ; X = frame
@copy:  lda (ptr2),y
        sta _frequency1,x
        lda (ptr3),y
        sta _frequency2,x
        lda (ptr4),y
        sta _frequency3,x
        lda _ampl1data,y
        sta _amplitude1,x
        lda _ampl2data,y
        sta _amplitude2,x
        lda _ampl3data,y
        sta _amplitude3,x
        lda _sampledConsonantFlags,y
        sta _sampledConsonantFlag,x
        lda _pitch
        clc
        adc ph1
        sta _pitches,x          ; pitches[X] = pitch + phase1
        inx
        dec ph2
        bne @copy
        stx rX
        inc m44
        beq transitions         ; (mem44 wrapped: the C loop ends too)
        jmp @phoneme

; ---------------------------------------------------------------- CREATE TRANSITIONS
transitions:
        lda #0
        sta rA
        sta m44
        sta m49
        sta rX
@pair:
        ldx rX
        lda _phonemeIndexOutput,x
        sta rY                  ; Y = current phoneme
        lda _phonemeIndexOutput+1,x
        sta rA                  ; A = next phoneme
        cmp #255
        bne @more
        jmp done
@more:  tax                     ; X = next phoneme
        lda _blendRank,x
        sta m56
        ldy rY
        lda _blendRank,y
        cmp m56
        bne @diff
        lda _outBlendLength,y   ; same rank: each phoneme's out-blend
        sta ph1
        lda _outBlendLength,x
        sta ph2
        jmp @lengths
@diff:  bcs @firstWeaker
        lda _inBlendLength,x    ; current phoneme ranks lower: next one's lengths
        sta ph1
        lda _outBlendLength,x
        sta ph2
        jmp @lengths
@firstWeaker:
        lda _outBlendLength,y   ; next phoneme ranks lower: current one's, swapped
        sta ph1
        lda _inBlendLength,y
        sta ph2
@lengths:
        ldy m44
        lda m49
        clc
        adc _phonemeLengthOutput,y
        sta m49                 ; mem49 = end of this phoneme
        clc
        adc ph2
        sta spc                 ; speedcounter = blend end
        lda m49
        sec
        sbc ph1
        sta ph3                 ; phase3 = blend start
        lda ph1
        clc
        adc ph2
        sta m38                 ; mem38 = total blend length
        sec
        sbc #2
        bpl @blendIt            ; (X - 2) & 128: too short to blend
        jmp @nextPair
@blendIt:
        lda #168
        sta m47

@table: lda m38
        sta m40                 ; mem40 = mem38
        ldx m47
        lda tablo-168,x
        sta ptr1
        lda tabhi-168,x
        sta ptr1+1
        cpx #168
        bne @notPitch
        ; pitch: from the middle of this phoneme to the middle of the next
        ldy m44
        lda _phonemeLengthOutput,y
        lsr
        sta m36
        lda _phonemeLengthOutput+1,y
        lsr
        sta m37
        clc
        adc m36
        sta m40                 ; mem40 = both halves
        lda m37
        clc
        adc m49
        sta m37                 ; centre of next phoneme
        lda m49
        sec
        sbc m36
        sta m36                 ; centre of this phoneme
        ldy m37
        lda (ptr1),y
        sta rA
        ldy m36
        sec
        sbc (ptr1),y
        sta m53                 ; mem53 = difference
        jmp @step
@notPitch:
        ldy spc
        lda (ptr1),y
        sta rA
        ldy ph3
        sec
        sbc (ptr1),y
        sta m53
@step:  ; step = difference / mem40 (C: signed, truncated toward zero),
        ; remainder for the Bresenham-style correction
        lda m53
        and #$80
        sta m50
        lda m53
        bpl @abs
        eor #$FF
        clc
        adc #1
@abs:   ; |difference| / mem40 -> quotient in A, remainder in mem51.
        ; Usually the difference is small, so subtract instead of an 8-step
        ; division; same results (mem40 == 0 keeps the shift division)
        ldx m40
        beq @shiftDiv
        cmp m40
        bcs @subtract
        sta m51                 ; smaller than mem40: quotient 0
        lda #0
        jmp @quotient
@subtract:
        ldx #0
@sub:   sec
        sbc m40
        inx
        cmp m40
        bcs @sub
        sta m51
        txa
        jmp @quotient
@shiftDiv:
        sta dvd
        lda #0
        ldx #8
@div:   asl dvd
        rol a
        cmp m40
        bcc @divNext
        sbc m40
        inc dvd
@divNext:
        dex
        bne @div
        sta m51                 ; mem51 = remainder
        lda dvd
@quotient:
        bit m50
        bpl @stepPos
        eor #$FF
        clc
        adc #1
@stepPos:
        sta m53                 ; mem53 = signed step

        ; interpolate frames phase3+1 .. phase3+mem40-1
        ldx m40
        ldy ph3
        lda #0
        sta m56
        lda m51
        bne @frame
        ; no remainder: the correction below never fires, so a plain loop
        ; gives the same frames
@plain: lda (ptr1),y
        clc
        adc m53
        iny
        dex
        beq @tableDone
        sta (ptr1),y
        jmp @plain
@frame: lda (ptr1),y
        clc
        adc m53
        sta m48                 ; mem48 = tab[Y] + step
        iny
        dex
        beq @tableDone
        lda m56
        clc
        adc m51
        sta m56
        cmp m40
        bcc @write
        sbc m40                 ; (carry set)
        sta m56
        bit m50
        bmi @down
        lda m48
        beq @write
        inc m48
        jmp @write
@down:  dec m48
@write: lda m48
        sta (ptr1),y
        jmp @frame
@tableDone:
        inc m47
        lda m47
        cmp #175
        beq @nextPair
        jmp @table

@nextPair:
        inc m44
        lda m44
        sta rX
        jmp @pair

done:   ; frames = mem49 + length of the last phoneme
        ldy m44
        lda m49
        clc
        adc _phonemeLengthOutput,y
        ldx #0
        rts

; ---------------------------------------------------------------- AddInflection
; Ramps pitch by dir per frame over the 30 frames before the punctuation
; (render.c AddInflection). Uses and leaves rX; leaves rA as the C does.
addInflection:
        lda rX
        sta m49                 ; mem49 = position of the punctuation
        cmp #31
        bcs @back
        lda #0                  ; within 30 frames of the start: from 0
        jmp @start
@back:  sec
        sbc #30
@start: tax
@skip:  lda _pitches,x          ; while ((A = pitches[X]) == 127) X++;
        cmp #127
        bne @ramp
        inx
        jmp @skip
@ramp:  clc
        adc dir                 ; A += direction
        sta aph
        sta _pitches,x
@next:  inx
        cpx m49
        beq @ret
        lda _pitches,x
        cmp #255
        beq @next
        lda aph
        jmp @ramp
@ret:   lda aph
        sta rA
        rts
