; render_fast.s - 6502 versions of render.c's whole-table loops for the C64
; build (cc65 compiles them to ~100 cycles per entry; these take ~15-20).
; Same results as the C loops they replace (see render.c, __CC65__ blocks).

        .export _PitchContour, _FemalePitchTable, _RescaleAmplitudes
        .import _pitches, _frequency1, _femalePitch
        .import _amplitude1, _amplitude2, _amplitude3, _amplitudeRescale
        .importzp tmp1, tmp2, tmp3

        .segment "CODE"

; pitches[i] -= frequency1[i] >> 1, for all 256 entries
_PitchContour:
        ldy #0
@loop:  lda _frequency1,y
        lsr
        sta tmp1
        lda _pitches,y
        sec
        sbc tmp1
        sta _pitches,y
        iny
        bne @loop
        rts

; pitches[i] = femalePitch[pitches[i]], for all 256 entries
_FemalePitchTable:
        ldy #0
@loop:  ldx _pitches,y
        lda _femalePitch,x
        sta _pitches,y
        iny
        bne @loop
        rts

; amplitudeN[i] = amplitudeRescale[amplitudeN[i]], N = 1..3, all 256 entries
_RescaleAmplitudes:
        ldy #0
@loop:  ldx _amplitude1,y
        lda _amplitudeRescale,x
        sta _amplitude1,y
        ldx _amplitude2,y
        lda _amplitudeRescale,x
        sta _amplitude2,y
        ldx _amplitude3,y
        lda _amplitudeRescale,x
        sta _amplitude3,y
        iny
        bne @loop
        rts

; ShiftUp3(position): sam.c Insert's shift of phonemeindex, phonemeLength and
; stress up by one from position. The C shifts everything up to entry 253;
; here only up to 16 entries past the end marker (255), since nothing reads
; beyond the marker plus one. Checked against the full C shift by
; tools/check_c64.py frames.
        .export _ShiftUp3
        .import _phonemeindex, _phonemeLength, _stress

_ShiftUp3:
        cmp #254
        bcs @none               ; position > 253: nothing to move
        sta tmp1
        tax                     ; find the end marker at or after position
@find:  lda _phonemeindex,x
        cmp #255
        beq @found
        inx
        cpx #254
        bcc @find
        ldx #253
        jmp @shift
@found: txa
        clc
        adc #16
        bcs @cap
        cmp #254
        bcc @top
@cap:   lda #253
@top:   tax
@shift: lda _phonemeindex,x
        sta _phonemeindex+1,x
        lda _phonemeLength,x
        sta _phonemeLength+1,x
        lda _stress,x
        sta _stress+1,x
        cpx tmp1
        beq @none
        dex
        jmp @shift
@none:  rts

; FinishFrames(n | flags << 8): render.c's three whole-table passes in one,
; over the phrase's n frames plus 4 (the C does all 256; entries past the
; phrase are not played). Per frame, in the C's order:
;   pitch contour   pitches -= frequency1 >> 1      (unless sing mode)
;   female pitch    pitches = femalePitch[pitches]  (female voice)
;   rescale         amplitudeN = amplitudeRescale[amplitudeN]
; A = n, X = flags: bit 0 sing mode, bit 1 female.
        .export _FinishFrames

_FinishFrames:
        stx tmp2
        clc
        adc #4
        bcc @count
        lda #0                  ; n + 4 > 255: all 256 entries
@count: sta tmp3
        ; pitch: contour unless sing mode, then the female register
        lda tmp2
        and #3
        beq @contourMale
        cmp #2
        beq @contourFemale
        cmp #3
        beq @femaleOnly
        jmp @rescale            ; sing mode, male: pitch untouched
@contourMale:
        ldy #0
@cm:    lda _frequency1,y
        lsr
        sta tmp1
        lda _pitches,y
        sec
        sbc tmp1
        sta _pitches,y
        iny
        cpy tmp3
        bne @cm
        jmp @rescale
@contourFemale:
        ldy #0
@cf:    lda _frequency1,y
        lsr
        sta tmp1
        lda _pitches,y
        sec
        sbc tmp1
        tax
        lda _femalePitch,x
        sta _pitches,y
        iny
        cpy tmp3
        bne @cf
        jmp @rescale
@femaleOnly:
        ldy #0
@fo:    ldx _pitches,y
        lda _femalePitch,x
        sta _pitches,y
        iny
        cpy tmp3
        bne @fo
@rescale:
        ldy #0
@rs:    ldx _amplitude1,y
        lda _amplitudeRescale,x
        sta _amplitude1,y
        ldx _amplitude2,y
        lda _amplitudeRescale,x
        sta _amplitude2,y
        ldx _amplitude3,y
        lda _amplitudeRescale,x
        sta _amplitude3,y
        iny
        cpy tmp3
        bne @rs
        rts
