; reciter_asm.s - SAM's text-to-phoneme rules engine (reciter.c
; TextToPhonemes) in 6502 assembly for the C64 / 6502 builds.
;
; int __fastcall__ TextToPhonemes(unsigned char *input);
;
; A statement-for-statement port of the C (which is itself a transliteration
; of the original 6502 code; the posNNNNN labels are the C's goto targets).
; C's globals X and Y are the X and Y registers here; A is the accumulator
; where the C relies on it. The rule tables are the C ones (ReciterTabs.h).
; tools/check_c64.py frames compares the result with the native build.

        .export _TextToPhonemes
        .import _tab36376
        .import RULE_ENDS, LETTER_START_LO, LETTER_START_HI, PUNCT_START  ; reciter_index.s
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4, sreg


inp     = ptr1          ; input / output buffer
rule    = ptr2          ; byte 0 of the current rule (SAM's mem62)
m57     = tmp1
m58     = tmp2
m59     = tmp3
m60     = tmp4
m61     = ptr3
m64     = ptr3+1
m65     = ptr4
m66     = ptr4+1
tp      = sreg          ; next entry in RULE_ENDS

        .segment "BSS"
inputtemp: .res 256     ; secure copy of the input (tab36096)
m56:    .res 1          ; output position
savey:  .res 1

        .segment "CODE"

; Code37055(mem59): X = mem59 - 1; A = tab36376[Y = inputtemp[X]]
.macro c37055
        ldx m59
        dex
        ldy inputtemp,x
        lda _tab36376,y
.endmacro

; Code37066(mem58): X = mem58 + 1; A = tab36376[Y = inputtemp[X]]
.macro c37066
        ldx m58
        inx
        ldy inputtemp,x
        lda _tab36376,y
.endmacro

_TextToPhonemes:
        sta inp
        stx inp+1
        lda #32
        sta inputtemp
        ; copy the input, folding to SAM's character set
        ldx #1
        ldy #0
@copy:  lda (inp),y
        and #127
        cmp #112
        bcc @lt112
        and #95
        jmp @put
@lt112: cmp #96
        bcc @put
        and #79
@put:   sta inputtemp,x
        inx
        iny
        cpy #255
        beq @copied
        cmp #'['                ; the C copies all 255 bytes; nothing reads
        bne @copy               ; further than a few past the '[' end marker
        lda #16
        sta m57
@tail:  lda (inp),y             ; 16 more, then stop
        and #127
        cmp #112
        bcc @tlt112
        and #95
        jmp @tput
@tlt112:
        cmp #96
        bcc @tput
        and #79
@tput:  sta inputtemp,x
        inx
        iny
        cpy #255
        beq @copied
        dec m57
        bne @tail
@copied:
        lda #27
        sta inputtemp+255
        lda #255
        sta m61
pos36550:
        lda #255
        sta m56

pos36554:
        inc m61
        ldx m61
        lda inputtemp,x
        sta m64
        cmp #'['
        bne @notEnd
        inc m56
        ldy m56
        lda #155
        sta (inp),y
        jmp return1
@notEnd:
        cmp #'.'
        bne pos36607
        inx
        ldy inputtemp,x
        lda _tab36376,y
        and #1
        bne pos36607
        inc m56
        ldy m56
        lda #'.'
        sta (inp),y
        jmp pos36554

pos36607:
        ldy m64
        lda _tab36376,y
        sta m57
        and #2
        beq @notPunct
        lda #<PUNCT_START       ; mem62 = 37541: the punctuation rules
        sta tp
        lda #>PUNCT_START
        sta tp+1
        jmp pos36700
@notPunct:
        lda m57
        bne pos36677
        lda #32
        sta inputtemp,x
        inc m56
        ldx m56
        cpx #121
        bcs pos36654
        txa
        tay
        lda #32
        sta (inp),y
        jmp pos36554

pos36654:
        txa
        tay
        lda #155
        sta (inp),y
        jmp return1

pos36677:
        lda m57
        and #128
        bne @letter
        jmp return0
@letter:
        ; the rules for this letter
        lda m64
        sec
        sbc #'A'
        tax
        lda LETTER_START_LO,x   ; mem62 = tab37489/tab37515[letter]
        sta tp
        lda LETTER_START_HI,x
        sta tp+1

; -------------------------------------------------- go to the next rule
pos36700:
        ; next rule: the C scans forward from mem62 + 1 to the next byte with
        ; bit 7 set; RULE_ENDS lists those bytes in order (gen_rule_index.py)
        ldy #0
        lda (tp),y
        sta rule
        iny
        lda (tp),y
        sta rule+1
        iny                     ; precomputed '(' ')' '=' positions
        lda (tp),y
        sta m66
        iny
        lda (tp),y
        sta m65
        iny
        lda (tp),y
        sta m64
        lda tp
        clc
        adc #5
        sta tp
        bcc @tpDone
        inc tp+1
@tpDone:
        lda m66
        beq @scan
        jmp @matchStart
@scan:  ldy #1                  ; Y = 1: the rule's first byte follows its predecessor's end
@open:  lda (rule),y            ; find '('
        cmp #'('
        beq @gotOpen
        iny
        jmp @open
@gotOpen:
        sty m66
@close: iny                     ; find ')'
        lda (rule),y
        cmp #')'
        bne @close
        sty m65
@eq:    iny                     ; find '='
        lda (rule),y
        and #127
        cmp #'='
        bne @eq
        sty m64
@matchStart:
        ldx m61
        stx m60
        ldy m66                 ; compare the text inside the brackets
        iny
@match: lda inputtemp,x
        sta m57
        lda (rule),y
        cmp m57
        beq @same
        jmp pos36700
@same:  iny
        cpy m65
        beq pos36787
        inx
        stx m60
        jmp @match

; -------------------------------------------------- match the left context
pos36787:
        lda m61
        sta m59
pos36791:
        dec m66
        ldy m66
        lda (rule),y
        sta m57
        bpl @plain
        jmp pos37180
@plain: and #127
        tax
        lda _tab36376,x
        and #128
        beq pos36833
        ldx m59
        dex
        lda inputtemp,x
        cmp m57
        beq @ok
        jmp pos36700
@ok:    stx m59
        jmp pos36791

pos36833:
        lda m57
        cmp #' '
        bne :+
        jmp pos36895
:
        cmp #'#'
        bne :+
        jmp pos36910
:
        cmp #'.'
        bne :+
        jmp pos36920
:
        cmp #'&'
        bne :+
        jmp pos36935
:
        cmp #'@'
        bne :+
        jmp pos36967
:
        cmp #'^'
        bne :+
        jmp pos37004
:
        cmp #'+'
        bne :+
        jmp pos37019
:
        cmp #':'
        bne :+
        jmp pos37040
:
        jmp return0

pos36895:
        c37055
        and #128
        beq pos36905
        jmp pos36700
pos36905:
        stx m59
        jmp pos36791

pos36910:
        c37055
        and #64
        bne pos36905
        jmp pos36700

pos36920:
        c37055
        and #8
        bne pos36930
        jmp pos36700
pos36930:
        stx m59
        jmp pos36791

pos36935:
        c37055
        and #16
        bne pos36930
        lda inputtemp,x
        cmp #72
        beq @h
        jmp pos36700
@h:     dex
        lda inputtemp,x
        cmp #67
        beq pos36930
        cmp #83
        beq pos36930
        jmp pos36700

pos36967:                       ; (the C's later test is always true: no match)
        c37055
        and #4
        bne pos36930
        jmp pos36700

pos37004:
        c37055
        and #32
        bne pos37014
        jmp pos36700
pos37014:
        stx m59
        jmp pos36791

pos37019:
        ldx m59
        dex
        lda inputtemp,x
        cmp #'E'
        beq pos37014
        cmp #'I'
        beq pos37014
        cmp #'Y'
        beq pos37014
        jmp pos36700

pos37040:
        c37055
        and #32
        bne @more
        jmp pos36791
@more:  stx m59
        jmp pos37040

; -------------------------------------------------- '%' suffixes
pos37077:
        ldx m58
        inx
        lda inputtemp,x
        cmp #'E'
        bne pos37157
        inx
        ldy inputtemp,x
        dex
        lda _tab36376,y
        and #128
        beq pos37108
        inx
        lda inputtemp,x
        cmp #'R'
        bne pos37113
pos37108:
        stx m58
        jmp pos37184
pos37113:
        cmp #83
        beq pos37108
        cmp #68
        beq pos37108
        cmp #76
        bne pos37135
        inx
        lda inputtemp,x
        cmp #89
        beq pos37108
        jmp pos36700
pos37135:
        cmp #70
        beq @f
        jmp pos36700
@f:     inx
        lda inputtemp,x
        cmp #85
        beq @fu
        jmp pos36700
@fu:    inx
        lda inputtemp,x
        cmp #76
        beq pos37108
        jmp pos36700
pos37157:
        cmp #73
        beq @i
        jmp pos36700
@i:     inx
        lda inputtemp,x
        cmp #78
        beq @in
        jmp pos36700
@in:    inx
        lda inputtemp,x
        cmp #71
        beq pos37108
        jmp pos36700

; -------------------------------------------------- match the right context
pos37180:
        lda m60
        sta m58
pos37184:
        ldy m65
        iny
        cpy m64
        bne @notDone
        jmp pos37455
@notDone:
        sty m65
        lda (rule),y
        sta m57
        tax
        lda _tab36376,x
        and #128
        beq pos37226
        ldx m58
        inx
        lda inputtemp,x
        cmp m57
        beq @ok
        jmp pos36700
@ok:    stx m58
        jmp pos37184

pos37226:
        lda m57
        cmp #32
        bne :+
        jmp pos37295
:
        cmp #35
        bne :+
        jmp pos37310
:
        cmp #46
        bne :+
        jmp pos37320
:
        cmp #38
        bne :+
        jmp pos37335
:
        cmp #64
        bne :+
        jmp pos37367
:
        cmp #94
        bne :+
        jmp pos37404
:
        cmp #43
        bne :+
        jmp pos37419
:
        cmp #58
        bne :+
        jmp pos37440
:
        cmp #37
        bne @bad
        jmp pos37077
@bad:   jmp return0

pos37295:
        c37066
        and #128
        beq pos37305
        jmp pos36700
pos37305:
        stx m58
        jmp pos37184

pos37310:
        c37066
        and #64
        bne pos37305
        jmp pos36700

pos37320:
        c37066
        and #8
        bne pos37330
        jmp pos36700
pos37330:
        stx m58
        jmp pos37184

pos37335:
        c37066
        and #16
        bne pos37330
        lda inputtemp,x
        cmp #72
        beq @h
        jmp pos36700
@h:     inx
        lda inputtemp,x
        cmp #67
        beq pos37330
        cmp #83
        beq pos37330
        jmp pos36700

pos37367:                       ; (as pos36967: no match)
        c37066
        and #4
        bne pos37330
        jmp pos36700

pos37404:
        c37066
        and #32
        bne pos37414
        jmp pos36700
pos37414:
        stx m58
        jmp pos37184

pos37419:
        ldx m58
        inx
        lda inputtemp,x
        cmp #69
        beq pos37414
        cmp #73
        beq pos37414
        cmp #89
        beq pos37414
        jmp pos36700

pos37440:
        c37066
        and #32
        bne @more
        jmp pos37184
@more:  stx m58
        jmp pos37440

; -------------------------------------------------- the rule matched: output it
pos37455:
        ldy m64
        lda m60
        sta m61
pos37461:
        lda (rule),y
        sta m57
        and #127
        cmp #'='
        beq @skip
        sty savey
        inc m56
        ldy m56
        sta (inp),y
        ldy savey
@skip:  bit m57
        bpl pos37485
        jmp pos36554
pos37485:
        iny
        jmp pos37461

return1:
        lda #1
        ldx #0
        rts
return0:
        lda #0
        ldx #0
        rts
