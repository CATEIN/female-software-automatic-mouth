#!/bin/sh
# Builds bench.elf with the arduino-pico core's compiler (run from pico/bench).
G=$(ls -d ~/AppData/Local/Arduino15/packages/rp2040/tools/pqt-gcc/*/bin)
OPT=${1:--Os}
"$G/arm-none-eabi-gcc" -mcpu=cortex-m0plus -mthumb $OPT -ffunction-sections -fdata-sections \
    -DKLATT_FIXED -DSAM_STREAM -DLEX_SMALL -I../../src -w -nostartfiles --specs=nosys.specs -Wl,--gc-sections -T bench.ld \
    -o bench.elf crt0.s bench.c ../../src/klatt.c ../../src/render.c ../../src/sam.c ../../src/reciter.c \
    ../../src/female.c ../../src/frames.c ../../src/debug.c ../../src/lexicon.c ../../src/rules.c ../../src/glottal.c -lm 2>&1 | grep -v "will always fail"
"$G/arm-none-eabi-nm" bench.elf > bench.sym
