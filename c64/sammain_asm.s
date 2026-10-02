; sammain_asm.s - the rest of SAM's parser in 6502 assembly (C64 / 6502
; builds): everything SAMMain does after Parser1.
;
; void SamParse(void);
;
;   Parser2          phoneme rules (diphthong glides, UL/UM/UN, T R -> CH R, ...)
;   CopyStress       stress of a stressed vowel onto the consonant before it
;   SetPhonemeLength lengths from the length tables
;   AdjustLengths    lengthen/shorten around punctuation, stops, voicing
;   Code41240        split plosives into their three parts
;   (check)          cut the list at the first invalid phoneme
;   InsertBreath     breaks (254) for breathing in long sentences
;   PrepareOutput    copy phrase by phrase to the output lists and Render()
;
; Each part follows its sam.c function step for step. C's global X carries
; over where the C relies on it (Code41240 -> the check loop); the C's reads
; of entry [pos-1] / [X-1] use "table-1,x" so a read before the array hits
; the same byte as the compiled C. sam.c's global mem59, passed to Insert as
; the length, is never written and is always 0. tools/check_c64.py frames
; compares the result with the native build.

        .export _SamParse
        .import _phonemeindex, _phonemeLength, _stress
        .import _phonemeIndexOutput, _phonemeLengthOutput, _stressOutput
        .import _flags, _flags2, _phonemeLengthTable, _phonemeStressedLengthTable
        .import _ShiftUp3, _Render

        .segment "BSS"
pos:    .res 1          ; Parser2 / CopyStress / Code41240 position
mem58:  .res 1
loopi:  .res 1          ; AdjustLengths' loopIndex
m56:    .res 1
idx:    .res 1
mem54:  .res 1          ; InsertBreath
mem55:  .res 1
mem66:  .res 1
savex:  .res 1
insPos: .res 1          ; Insert(position, phoneme, length, stress)
insPh:  .res 1
insLen: .res 1
insSt:  .res 1
insX:   .res 1
insY:   .res 1

        .segment "CODE"

_SamParse:
        jsr parser2
        jsr copyStress
        jsr setPhonemeLength
        jsr adjustLengths       ; leaves C's X in X
        jsr code41240           ; uses and leaves C's X in X
        ; SAMMain: cut the list at the first phoneme > 80, from C's X on
@check: lda _phonemeindex,x
        cmp #81
        bcc @valid
        lda #255
        sta _phonemeindex,x
        jmp @checked
@valid: inx
        bne @check
@checked:
        jsr insertBreath
        jmp prepareOutput

; ---------------------------------------------------------------- Insert
; sam.c Insert(insPos, insPh, insLen, insSt); keeps X and Y like the C.
insert:
        stx insX
        sty insY
        lda insPos
        jsr _ShiftUp3
        ldx insPos
        lda insPh
        sta _phonemeindex,x
        lda insLen
        sta _phonemeLength,x
        lda insSt
        sta _stress,x
        ldx insX
        ldy insY
        rts

; ---------------------------------------------------------------- Parser2
parser2:
        lda #0
        sta pos
        sta mem58
p2next0:
        ldx pos                 ; X = pos
        lda _phonemeindex,x     ; A = phonemeindex[pos]
        bne @notPause
        inc pos
        jmp p2next0
@notPause:
        cmp #255
        bne @go
        rts
@go:    tay                     ; Y = A
        lda _flags,y
        and #16                 ; diphthong?
        beq pos41457
        lda _stress,x
        sta mem58
        sta insSt
        lda _flags,y
        and #32
        beq @wx
        lda #21                 ; 'YX'
        jmp @glide
@wx:    lda #20                 ; 'WX'
@glide: sta insPh
        lda pos
        clc
        adc #1
        sta insPos
        lda #0
        sta insLen
        jsr insert
        ldx pos
        jmp pos41749

pos41457:
        lda _phonemeindex,x
        cmp #78                 ; 'UL' -> 'AX L'
        bne pos41487
        lda #24
pos41466:
        sta insPh
        lda _stress,x
        sta mem58
        sta insSt
        lda #13                 ; 'AX'
        sta _phonemeindex,x
        txa
        clc
        adc #1
        sta insPos
        lda #0
        sta insLen
        jsr insert
        jmp p2next
pos41487:
        cmp #79                 ; 'UM' -> 'AX M'
        bne pos41495
        lda #27
        jmp pos41466
pos41495:
        cmp #80                 ; 'UN' -> 'AX N'
        bne pos41503
        lda #28
        jmp pos41466

pos41503:                       ; stressed vowel, pause, stressed vowel: glottal stop
        tay
        lda _flags,y
        and #128
        beq @noStop
        lda _stress,x
        beq @noStop
        inx
        lda _phonemeindex,x
        bne @noStop
        inx
        ldy _phonemeindex,x
        cpy #255
        bne @flagsY
        lda #0                  ; 65 & 128
        jmp @isVowel
@flagsY:
        lda _flags,y
        and #128
@isVowel:
        beq @noStop
        lda _stress,x
        beq @noStop
        stx insPos              ; Insert(X, 'Q*', 0, 0)
        lda #31
        sta insPh
        lda #0
        sta insLen
        sta insSt
        jsr insert
        jmp p2next
@noStop:
        ldx pos
        lda _phonemeindex,x
        cmp #23                 ; 'R'
        bne pos41611
        dex                     ; X--
        ldy pos
        lda _phonemeindex-1,y   ; A = phonemeindex[pos-1]
        cmp #69                 ; 'T' R -> 'CH' R
        bne @notT
        lda #42
        sta _phonemeindex-1,y
        lda #69                 ; (A stays 'T')
        jmp pos41779
@notT:  cmp #57                 ; 'D' R -> 'J' R
        bne @notD
        lda #44
        sta _phonemeindex-1,y
        lda #57                 ; (A stays 'D')
        jmp pos41788
@notD:  tay
        lda _flags,y
        and #128
        beq @rDone
        ldy pos
        lda #18                 ; vowel R -> 'RX'
        sta _phonemeindex,y
@rDone: jmp p2next

pos41611:
        cmp #24                 ; 'L' after a vowel -> 'LX'
        bne @notL
        ldy pos
        lda _phonemeindex-1,y
        tay
        lda _flags,y
        and #128
        bne @lx
        jmp p2next
@lx:    lda #19
        sta _phonemeindex,x
        jmp p2next
@notL:  cmp #32                 ; 'G' 'S' -> 'G' 'Z'
        bne @notS
        ldy pos
        lda _phonemeindex-1,y
        cmp #60
        beq @z
        jmp p2next
@z:     lda #38
        sta _phonemeindex,y
        jmp p2next
@notS:  cmp #72                 ; 'K' not before a front vowel -> 'KX'
        bne @notK
        ldy pos
        lda _phonemeindex+1,y
        cmp #255
        beq @kx
        tay
        lda _flags,y
        and #32
        bne @afterKG
@kx:    ldy pos
        lda #75
        sta _phonemeindex,y
        jmp @afterKG
@notK:  cmp #60                 ; 'G' not before a front vowel -> 'GX'
        bne @afterKG
        ldy pos
        lda _phonemeindex+1,y
        cmp #255
        bne @gIdx
        jmp p2next
@gIdx:  tay
        lda _flags,y
        and #32
        beq @gx
        jmp p2next
@gx:    ldy pos
        lda #63
        sta _phonemeindex,y
        jmp p2next
@afterKG:                       ; 'S' + unvoiced plosive -> voiced plosive
        ldy pos
        lda _phonemeindex,y
        tay                     ; Y = phonemeindex[pos]
        lda _flags,y
        and #1
        bne @plosive
        jmp pos41749
@plosive:
        stx savex
        ldx pos
        lda _phonemeindex-1,x
        ldx savex
        cmp #32
        beq @sPlosive
        tya                     ; A = Y
        jmp pos41812
@sPlosive:
        tya
        sec
        sbc #12
        ldy pos
        sta _phonemeindex,y
        jmp p2next

pos41749:
        lda _phonemeindex,x
        cmp #53                 ; 'UW' after an alveolar -> 'UX'
        bne pos41779
        ldy _phonemeindex-1,x
        lda _flags2,y
        and #4
        bne @ux
        jmp p2next
@ux:    lda #16
        sta _phonemeindex,x
        jmp p2next
pos41779:
        cmp #42                 ; 'CH' -> 'CH' 'CH''
        bne pos41788
        jmp twoPart
pos41788:
        cmp #44                 ; 'J' -> 'J' 'J''
        bne pos41812
twoPart:
        clc
        adc #1
        sta insPh
        txa
        clc
        adc #1
        sta insPos
        lda #0
        sta insLen
        lda _stress,x
        sta insSt
        jsr insert
        jmp p2next

pos41812:                       ; 'T'/'D' between vowels -> flap 'DX'
        cmp #69
        beq @td
        cmp #57
        beq @td
        jmp p2next
@td:    ldy _phonemeindex-1,x
        lda _flags,y
        and #128
        bne @afterVowel
        jmp p2next
@afterVowel:
        inx
        lda _phonemeindex,x
        beq @pause
        tay
        lda _flags,y
        and #128
        bne @nextVowel
        jmp p2next
@nextVowel:
        lda _stress,x
        beq @dx
        jmp p2next
@pause: ldy _phonemeindex+1,x
        cpy #255
        bne @pauseIdx
        jmp p2next              ; 65 & 128 == 0
@pauseIdx:
        lda _flags,y
        and #128
        bne @dx
        jmp p2next
@dx:    ldy pos
        lda #30
        sta _phonemeindex,y
p2next: inc pos
        jmp p2next0

; ---------------------------------------------------------------- CopyStress
copyStress:
        lda #0
        sta pos
@loop:  ldx pos
        ldy _phonemeindex,x
        cpy #255
        bne @more
        rts
@more:  lda _flags,y
        and #64                 ; consonant?
        beq @next
        ldy _phonemeindex+1,x
        cpy #255
        beq @next
        lda _flags,y
        and #128                ; followed by a vowel?
        beq @next
        ldy _stress+1,x
        beq @next
        tya
        bmi @next
        iny
        tya
        sta _stress,x           ; stress + 1
@next:  inc pos
        jmp @loop

; ---------------------------------------------------------------- SetPhonemeLength
setPhonemeLength:
        ldx #0
@loop:  ldy _phonemeindex,x
        cpy #255
        beq @done
        lda _stress,x
        beq @plain
        bmi @plain
        lda _phonemeStressedLengthTable,y
        jmp @set
@plain: lda _phonemeLengthTable,y
@set:   sta _phonemeLength,x
        inx
        jmp @loop
@done:  rts

; ---------------------------------------------------------------- AdjustLengths
adjustLengths:
        ldx #0
@punct: ldy _phonemeindex,x     ; find punctuation
        cpy #255
        beq @second
        lda _flags2,y
        and #1
        bne @found
        inx
        jmp @punct
@found: stx loopi
@back:  dex                     ; back to the previous vowel
        beq @second             ; (X == 0: done with the first pass)
        ldy _phonemeindex,x
        cpy #255
        beq @lengthen
        lda _flags,y
        and #128
        beq @back
@lengthen:                      ; lengthen everything up to the punctuation
        ldy _phonemeindex,x
        cpy #255
        beq @step
        lda _flags2,y
        and #32
        beq @long
        lda _flags,y
        and #4
        beq @step
@long:  lda _phonemeLength,x    ; length * 1.5 + 1
        lsr
        sec
        adc _phonemeLength,x
        sta _phonemeLength,x
@step:  inx
        cpx loopi
        bne @lengthen
        inx
        jmp @punct

@second:
        lda #0
        sta loopi
al2:    ldx loopi
        ldy _phonemeindex,x
        cpy #255
        bne @more
        rts                     ; (X = loopIndex, as the C leaves it)
@more:  lda _flags,y
        and #128                ; vowel?
        bne @vowel
        jmp @notVowel
@vowel: inx
        ldy _phonemeindex,x
        cpy #255
        bne @fl
        lda #65
        jmp @m56
@fl:    lda _flags,y
@m56:   sta m56
        lda _flags,y            ; (flags[index], also for the end marker)
        and #64
        bne @voicedNext
        cpy #18                 ; RX / LX, then a voiced phoneme: shorten
        beq @rxlx
        cpy #19
        beq @rxlx
        jmp @next
@rxlx:  inx
        ldy _phonemeindex,x
        lda _flags,y
        and #64
        bne @shorten1
        jmp @next
@shorten1:
        ldy loopi
        lda _phonemeLength,y
        sec
        sbc #1
        sta _phonemeLength,y
        jmp @next
@voicedNext:
        lda m56
        and #4
        bne @lengthen54
        lda m56
        and #1
        bne @shorten8
        jmp @next
@shorten8:                      ; length -= length / 8
        dex
        lda _phonemeLength,x
        lsr
        lsr
        lsr
        sta m56
        lda _phonemeLength,x
        sec
        sbc m56
        sta _phonemeLength,x
        jmp @next
@lengthen54:                    ; length * 1.25 + 1
        lda _phonemeLength-1,x
        lsr
        lsr
        sec
        adc _phonemeLength-1,x
        sta _phonemeLength-1,x
        jmp @next

@notVowel:
        lda _flags2,y
        and #8
        beq @notNasal
        inx
        ldy _phonemeindex,x
        cpy #255
        beq @next               ; 65 & 2 == 0
        lda _flags,y
        and #2                  ; followed by a stop?
        beq @next
        lda #6
        sta _phonemeLength,x
        lda #5
        sta _phonemeLength-1,x
        jmp @next
@notNasal:
        lda _flags,y
        and #2                  ; stop consonant?
        beq @notStop
@skip:  inx
        ldy _phonemeindex,x
        beq @skip
        cpy #255
        beq @next               ; (65 & 2) == 0
        lda _flags,y
        and #2
        beq @next
        lda _phonemeLength,x    ; two stops: both length/2 + 1
        lsr
        clc
        adc #1
        sta _phonemeLength,x
        ldx loopi
        lda _phonemeLength,x
        lsr
        clc
        adc #1
        sta _phonemeLength,x
        jmp @next
@notStop:
        lda _flags2,y
        and #16
        beq @next
        ldy _phonemeindex-1,x
        lda _flags,y
        and #2
        beq @next
        lda _phonemeLength,x
        sec
        sbc #2
        sta _phonemeLength,x
@next:  inc loopi
        jmp al2

; ---------------------------------------------------------------- Code41240
code41240:
        lda #0
        sta pos
@loop:  ldy pos
        lda _phonemeindex,y
        cmp #255
        bne @body
        rts                     ; (X as the last pass left it)
@body:  ldx pos                 ; X = pos
        sta idx
        tay
        lda _flags,y
        and #2                  ; plosive?
        bne @plosive
        inc pos
        jmp @loop
@plosive:
        lda _flags,y
        and #1
        beq @split
@skip:  inx                     ; next non-pause phoneme
        lda _phonemeindex,x
        beq @skip
        cmp #255
        beq @split
        tay
        lda _flags,y
        and #8
        beq @notNasal
        inc pos
        jmp @loop
@notNasal:
        cpy #36                 ; '/H'
        beq @keep
        cpy #37                 ; '/X'
        bne @split
@keep:  inc pos
        jmp @loop
@split: ldy pos                 ; Insert(pos+1, index+1, ...), Insert(pos+2, index+2, ...)
        lda _stress,y
        sta insSt
        iny
        sty insPos
        ldy idx
        iny
        sty insPh
        lda _phonemeLengthTable,y
        sta insLen
        jsr insert
        ldy pos
        lda _stress,y
        sta insSt
        iny
        iny
        sty insPos
        ldy idx
        iny
        iny
        sty insPh
        lda _phonemeLengthTable,y
        sta insLen
        jsr insert
        lda pos
        clc
        adc #3
        sta pos
        jmp @loop

; ---------------------------------------------------------------- InsertBreath
insertBreath:
        lda #255
        sta mem54
        lda #0
        sta mem55
        sta mem66
@loop:  ldx mem66
        ldy _phonemeindex,x
        cpy #255
        bne @more
        rts
@more:  lda mem55
        clc
        adc _phonemeLength,x
        sta mem55
        cmp #232
        bcs @long
        cpy #254
        beq @notPunct
        lda _flags2,y
        and #1
        beq @notPunct
        inx                     ; break after the punctuation
        lda #0
        sta mem55
        stx insPos
        jsr insertBreak
        inc mem66
        inc mem66
        jmp @loop
@notPunct:
        cpy #0
        bne @n
        stx mem54               ; last pause
@n:     inc mem66
        jmp @loop
@long:  ldx mem54               ; too long: glottal stop at the last pause
        lda #31
        sta _phonemeindex,x
        lda #4
        sta _phonemeLength,x
        lda #0
        sta _stress,x
        inx
        lda #0
        sta mem55
        stx insPos
        jsr insertBreak
        inx
        stx mem66
        jmp @loop

insertBreak:                    ; Insert(insPos, 254, 0, 0)
        lda #254
        sta insPh
        lda #0
        sta insLen
        sta insSt
        jmp insert

; ---------------------------------------------------------------- PrepareOutput
prepareOutput:
        ldx #0
        ldy #0
@loop:  lda _phonemeindex,x
        cmp #255
        bne @not255
        lda #255
        sta _phonemeIndexOutput,y
        jmp _Render             ; last phrase
@not255:
        cmp #254                ; break: render this phrase
        bne @notBreak
        inx
        stx savex
        lda #255
        sta _phonemeIndexOutput,y
        jsr _Render
        ldx savex
        ldy #0
        jmp @loop
@notBreak:
        cmp #0
        bne @copy
        inx
        jmp @loop
@copy:  sta _phonemeIndexOutput,y
        lda _phonemeLength,x
        sta _phonemeLengthOutput,y
        lda _stress,x
        sta _stressOutput,y
        inx
        iny
        jmp @loop
