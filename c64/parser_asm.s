; parser_asm.s - sam.c's Parser1 in 6502 assembly (C64 / 6502 builds).
;
; int Parser1(void);
;
; Turns the phoneme text in input[] (e.g. "/HEH3LOW") into phoneme numbers
; (phonemeindex[]) and stress values (stress[]), with the same results as
; the C:
;   1. a two-letter name from signInputTable1/2 (second letter not '*')
;   2. otherwise a one-letter name (second letter '*')
;   3. otherwise a stress digit for the previous phoneme, else failure
; The C scans the 81-entry tables for each step; here the first match comes
; from per-character tables (parser_index.s, tools/gen_parser_index.py).
; The C's final A/X/Y do not matter: Parser2 sets them before use.

        .export _Parser1
        .import _input, _phonemeindex, _stress
        .import TWO_START, TWO_SECOND, TWO_PHONEME, ONE, STRESS
        .importzp tmp1, tmp2, tmp3

sign1   = tmp1
sign2   = tmp2
pos     = tmp3              ; next phonemeindex position

        .segment "CODE"

_Parser1:
        lda #0                  ; clear the stress table
        tax
@clear: sta _stress,x
        inx
        bne @clear
        sta pos
        ; X = input position

next:   lda _input,x
        sta sign1
        cmp #155                ; end of input
        bne @more
        ldy pos
        lda #255
        sta _phonemeindex,y     ; mark the end
        lda #1
        ldx #0
        rts
@more:  inx
        lda _input,x
        sta sign2

        ldy sign1               ; two-letter names starting with sign1
        lda TWO_START,y
        tay
@two:   lda TWO_SECOND,y
        beq @one                ; end of the list
        cmp sign2
        beq @twoMatch
        iny
        jmp @two
@twoMatch:
        lda TWO_PHONEME,y
        ldy pos
        sta _phonemeindex,y
        inc pos
        inx                     ; both letters used
        jmp next

@one:   ldy sign1               ; one-letter name?
        lda ONE,y
        cmp #255
        beq @stress
        ldy pos
        sta _phonemeindex,y
        inc pos
        jmp next                ; one letter used

@stress:
        ldy sign1               ; a stress digit?
        lda STRESS,y
        bne @isStress
        lda #0                  ; not a phoneme
        tax
        rts
@isStress:
        ldy pos
        dey
        sta _stress,y           ; stress of the previous phoneme
        jmp next
