    .syntax unified
    .cpu cortex-m0plus
    .thumb
    .global _start
    .thumb_func
_start:
    ldr r0, =__stack_top
    mov sp, r0
    ldr r0, =__data_start
    ldr r1, =__data_end
    ldr r2, =__data_load
1:  cmp r0, r1
    bhs 2f
    ldr r3, [r2]
    str r3, [r0]
    adds r0, #4
    adds r2, #4
    b 1b
2:  ldr r0, =__bss_start
    ldr r1, =__bss_end
    movs r2, #0
3:  cmp r0, r1
    bhs 4f
    str r2, [r0]
    adds r0, #4
    b 3b
4:  bl main
5:  b 5b
