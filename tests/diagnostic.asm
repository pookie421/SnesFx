; Original 65C816 LoROM fixture. NASM emits guest opcodes explicitly.
BITS 16
ORG 0x8000

%macro store8 2
    db 0xa9, %2
    db 0x8d
    dw %1
%endmacro

%macro expect8 2
    db 0xc9, %1
    db 0xf0, 3
    db 0x4c
    dw %2
%endmacro

%macro expect16 2
    db 0xc9
    dw %1
    db 0xf0, 3
    db 0x4c
    dw %2
%endmacro

reset:
    db 0x78, 0x18, 0xfb                ; SEI, CLC, XCE
    db 0xc2, 0x30                      ; 16-bit accumulator and index
    db 0xa2
    dw 0x1fef
    db 0x9a                            ; TXS
    db 0xa9
    dw 0x1234
    db 0x18, 0x69
    dw 0x4321
    expect16 0x5555, fail16
    db 0xf8, 0x18, 0xa9
    dw 0x0499
    db 0x69
    dw 0x0501
    expect16 0x1000, fail16
    db 0xd8, 0xe2, 0x20                ; CLD, 8-bit accumulator

    ; Start long DMA into upper WRAM before the line-1 H/V IRQ deadline.
    store8 0x2181, 0
    store8 0x2182, 0
    store8 0x2183, 1
    store8 0x4300, 0
    store8 0x4301, 0x80
    store8 0x4302, dma_source & 255
    store8 0x4303, dma_source >> 8
    store8 0x4304, 0
    store8 0x4305, 0
    store8 0x4306, 2
    store8 0x4207, 16
    store8 0x4208, 0
    store8 0x4209, 1
    store8 0x420a, 0
    store8 0x4200, 0x30
    db 0x58, 0xea                      ; CLI, NOP
    store8 0x420b, 1
    db 0xad
    dw 0x1ff4
    expect8 0x5a, fail
    db 0xaf, 0x00, 0x00, 0x7f          ; LDA $7f0000
    expect8 0x43, fail
    db 0xaf, 0xff, 0x01, 0x7f          ; LDA $7f01ff
    expect8 0x91, fail

    ; Force blank, set two CGRAM entries and upload one 2bpp tile via DMA.
    store8 0x2100, 0x80
    store8 0x2121, 0
    store8 0x2122, 0x20
    store8 0x2122, 0x0c
    store8 0x2122, 0x9f
    store8 0x2122, 0x43
    store8 0x2115, 0x80
    store8 0x2116, 0
    store8 0x2117, 0
    store8 0x4300, 1
    store8 0x4301, 0x18
    store8 0x4302, tile_source & 255
    store8 0x4303, tile_source >> 8
    store8 0x4304, 0
    store8 0x4305, 16
    store8 0x4306, 0
    store8 0x420b, 1
    store8 0x2105, 0
    store8 0x2107, 4
    store8 0x210b, 0
    store8 0x212c, 1
    store8 0x2100, 15

    ; Wait for VBlank, then leave a human-readable verdict in WRAM.
.wait_frame:
    db 0xad
    dw 0x4212
    db 0x10, .wait_frame - ($ + 2)
%ifdef TEST_APU
    db 0xad
    dw 0x2140
    expect8 0xa7, fail
    store8 0x2141, 0x59
    db 0xa2
    dw 0x0400
.wait_apu:
    db 0xad
    dw 0x2142
    db 0xc9, 0x96
    db 0xf0, .apu_answer - ($ + 2)
    db 0xca
    db 0xd0, .wait_apu - ($ + 2)
    db 0x4c
    dw fail
.apu_answer:
%endif
    store8 0x1ff0, 'P'
    store8 0x1ff1, 'A'
    store8 0x1ff2, 'S'
    store8 0x1ff3, 'S'
    db 0xdb                            ; STP

fail16:
    db 0xe2, 0x20
fail:
    store8 0x1ff0, 'F'
    store8 0x1ff1, 'A'
    store8 0x1ff2, 'I'
    store8 0x1ff3, 'L'
    db 0xdb

irq:
    db 0x48                            ; PHA
    db 0xad
    dw 0x4211
    db 0x10, .done - ($ + 2)           ; BPL if IRQ flag missing
    db 0xad
    dw 0x4305
    db 0xd0, .done - ($ + 2)           ; DMA must be finished
    db 0xad
    dw 0x4306
    db 0xd0, .done - ($ + 2)
    db 0xaf, 0xff, 0x01, 0x7f
    expect8 0x91, .done
    store8 0x1ff4, 0x5a
.done:
    db 0x68, 0x40                      ; PLA, RTI

tile_source equ 0x8000 + ($ - $$)
    db 0xf0,0, 0xf0,0, 0xf0,0, 0xf0,0
    db 0x0f,0, 0x0f,0, 0x0f,0, 0x0f,0
dma_source equ 0x8000 + ($ - $$)
    db 0x43
    times 510 db 0
    db 0x91

times 0x7fc0 - ($ - $$) db 0
db 'X86 SNES DIAGNOSTIC  '
db 0x20, 0, 5, 0, 1, 0, 0
dw 0xffff, 0
times 0x7fee - ($ - $$) db 0
dw irq
times 0x7ffc - ($ - $$) db 0
dw reset, irq
