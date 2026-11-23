; Standalone PPU regression test. Exit 0 on success or the failing check ID.
; Linux: nasm -I src/ -f bin tests/ppu_test.asm -o build/ppu-test
; macOS: nasm -I src/ -DHOST_MAC=1 -f bin tests/ppu_test.asm -o build/ppu-test
; Run with chmod +x build/ppu-test, then ./build/ppu-test (x86-64).
; Flat executable segments keep state addresses below 4 GiB.
bits 64
default abs
[warning -reloc-abs-dword]
[warning -reloc-abs-qword]
IMAGE_BASE equ 0x400000
PAGE_SIZE equ 4096
org IMAGE_BASE

%ifdef HOST_MAC
    SYS_EXIT equ 0x2000001
%else
    SYS_EXIT equ 60
%endif

file_start:
%ifdef HOST_MAC
%macro MACH_SEGMENT 7
    dd 0x19, 72
    %%name: db %1
    times 16 - ($ - %%name) db 0
    dq %2, %3, %4, %5
    dd %6, %7, 0, 0
%endmacro
    dd 0xfeedfacf, 0x01000007, 3, 2, 5, mach_commands_end - mach_commands, 1, 0
mach_commands:
    MACH_SEGMENT "__PAGEZERO", 0, IMAGE_BASE, 0, 0, 0, 0
    MACH_SEGMENT "__TEXT", IMAGE_BASE, data_start - file_start, 0, data_start - file_start, 5, 5
    MACH_SEGMENT "__DATA", data_start, data_memory_size, data_start - file_start, data_end - data_start, 3, 3
    dd 0x32, 24, 1, 0x000a0f00, 0x000a0f00, 0
    dd 0x05, 184, 4, 42
    times 16 dq 0
    dq start, 0, 0, 0, 0
mach_commands_end:
%else
    db 0x7f, "ELF", 2, 1, 1, 0
    times 8 db 0
    dw 2, 62
    dd 1
    dq start, elf_segments - file_start, 0
    dd 0
    dw 64, 56, 2, 0, 0, 0
elf_segments:
    dd 1, 5
    dq 0, IMAGE_BASE, IMAGE_BASE, data_start - file_start
    dq data_start - file_start, PAGE_SIZE
    dd 1, 6
    dq data_start - file_start, data_start, data_start
    dq data_end - data_start, data_memory_size, PAGE_SIZE
%endif

%macro PUSH32 1
    lea rsp, [rsp - 8]
    mov dword [rsp], %1
%endmacro

%macro POP32 1
    mov %1, dword [rsp]
    lea rsp, [rsp + 8]
%endmacro

%macro write_ppu 2
    mov eax, %1
    mov edx, %2
    call ppu_write
%endmacro

%macro fail_if_ne 3
    cmp %1, %2
    je %%success
    mov edi, %3
    jmp exit
%%success:
%endmacro

start:
    cld
    call ppu_reset
    ; Check word-port data, remap, address increment and buffered readback.
    write_ppu 0x2115, 0x84
    write_ppu 0x2116, 0x21
    write_ppu 0x2117, 0
    write_ppu 0x2118, 0x35
    write_ppu 0x2119, 0xda
    fail_if_ne word [ppu_vram + 18], 0xda35, 10
    fail_if_ne word [ppu_vram_addr], 0x22, 11
    write_ppu 0x2116, 0x21
    mov eax, 0x2139
    call ppu_read
    fail_if_ne eax, 0x35, 12
    mov eax, 0x213a
    call ppu_read
    fail_if_ne eax, 0xda, 13
    ; CGRAM palette entries commit in pairs and discard the high bit.
    write_ppu 0x2121, 1
    write_ppu 0x2122, 0x1f
    fail_if_ne word [ppu_cgram + 2], 0, 14
    write_ppu 0x2122, 0x80
    fail_if_ne word [ppu_cgram + 2], 0x1f, 15
    write_ppu 0x2121, 128 + 1
    write_ppu 0x2122, 0
    write_ppu 0x2122, 0x7c
    ; Upload Mode 0 tile at word 0, tilemap at word 1024.
    write_ppu 0x2115, 0x80
    write_ppu 0x2116, 0
    write_ppu 0x2117, 0
    write_ppu 0x2118, 0x80
    write_ppu 0x2119, 0
    write_ppu 0x210e, 0xff
    write_ppu 0x210e, 0xff
    write_ppu 0x2107, 4
    write_ppu 0x212c, 1
    write_ppu 0x2100, 15
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x001f, 16
    fail_if_ne word [video_pixels + 2], 0, 17
    ; Select an OBJ tile with one blue pixel at x=3 on the first line.
    lea edi, [ppu_oam]
    mov ecx, 128
    mov eax, 0x0000c000
    rep stosd
    write_ppu 0x2100, 0x80
    write_ppu 0x2101, 1
    write_ppu 0x2102, 0
    write_ppu 0x2103, 0
    write_ppu 0x2104, 3
    fail_if_ne byte [ppu_oam], 0, 18
    write_ppu 0x2104, 0
    write_ppu 0x2104, 0
    write_ppu 0x2104, 0
    write_ppu 0x2116, 0
    write_ppu 0x2117, 0x20
    write_ppu 0x2118, 0x80
    write_ppu 0x2119, 0
    write_ppu 0x212c, 0x11
    write_ppu 0x2100, 15
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels + 6], 0x7c00, 19
    fail_if_ne word [video_pixels + 8], 0, 20
    write_ppu 0x2100, 8
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x11, 21
    fail_if_ne word [video_pixels + 6], 0x4400, 22
    write_ppu 0x2100, 0x80
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0, 23
    fail_if_ne word [video_pixels + 6], 0, 24
    ; Mode 0: red main BG1, green subscreen BG2, and a half-add.
    write_ppu 0x2108, 4
    write_ppu 0x2110, 0xff
    write_ppu 0x2110, 0xff
    write_ppu 0x2121, 33
    write_ppu 0x2122, 0xe0
    write_ppu 0x2122, 0x03
    write_ppu 0x212c, 1
    write_ppu 0x212d, 2
    write_ppu 0x2130, 2
    write_ppu 0x2131, 1
    write_ppu 0x2100, 15
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x03ff, 25
    write_ppu 0x2131, 0x41
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x01ef, 26
    ; Transparent subscreen uses fixed red without halving.
    write_ppu 0x212d, 0
    write_ppu 0x2132, 0x27
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x001f, 27
    write_ppu 0x2131, 0xc1
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x0018, 28
    ; Layer window masks the one eligible pixel on main.
    write_ppu 0x2130, 0
    write_ppu 0x2131, 0
    write_ppu 0x2123, 2
    write_ppu 0x2126, 0
    write_ppu 0x2127, 0
    write_ppu 0x212e, 1
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0, 29
    write_ppu 0x2123, 3
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x001f, 30
    ; Color window clips main outside of pixel 1 and prevents inside math.
    write_ppu 0x2100, 0x80
    write_ppu 0x2116, 0
    write_ppu 0x2117, 0
    write_ppu 0x2118, 0xc0
    write_ppu 0x2119, 0
    write_ppu 0x2100, 15
    write_ppu 0x212e, 0
    write_ppu 0x2125, 0x20
    write_ppu 0x2126, 1
    write_ppu 0x2127, 1
    write_ppu 0x2130, 0x60
    write_ppu 0x2131, 1
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 7, 31
    fail_if_ne word [video_pixels + 2], 0x001f, 32
    fail_if_ne word [video_pixels + 4], 0, 33
    ; Mode 7 identity matrix, nonzero tile index, and low-byte tilemap.
    write_ppu 0x2100, 0x80
    write_ppu 0x2105, 7
    write_ppu 0x2130, 0
    write_ppu 0x2131, 0
    write_ppu 0x210d, 0
    write_ppu 0x210d, 0
    write_ppu 0x210e, 0
    write_ppu 0x210e, 0
    write_ppu 0x211b, 0
    write_ppu 0x211b, 1
    write_ppu 0x211c, 0
    write_ppu 0x211c, 0
    write_ppu 0x211d, 0
    write_ppu 0x211d, 0
    write_ppu 0x211e, 0
    write_ppu 0x211e, 1
    write_ppu 0x211f, 0
    write_ppu 0x211f, 0
    write_ppu 0x2120, 0
    write_ppu 0x2120, 0
    mov byte [ppu_vram], 1
    mov byte [ppu_vram + 31 * 2], 1
    mov byte [ppu_vram + 72 * 2 + 1], 0x82
    mov byte [ppu_vram + 79 * 2 + 1], 0x02
    mov byte [ppu_vram + 8 * 2 + 1], 0x02
    mov word [ppu_cgram + 0x82 * 2], 0x001f
    mov word [ppu_cgram + 0x02 * 2], 0x7c00
    write_ppu 0x2100, 15
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x001f, 34
    fail_if_ne word [video_pixels + 2], 0, 35
    ; EXTBG priority bit and palette selection.
    write_ppu 0x2133, 0x40
    write_ppu 0x212c, 3
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x7c00, 36
    write_ppu 0x212c, 1
    write_ppu 0x2130, 1
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x4008, 37
    ; Horizontal flip and out-of-bounds policies.
    write_ppu 0x2130, 0
    write_ppu 0x211a, 1
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x7c00, 38
    write_ppu 0x210d, 0xf8
    write_ppu 0x210d, 0xff
    write_ppu 0x211a, 0x80
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0, 39
    fail_if_ne word [video_pixels + 16], 0x001f, 40
    write_ppu 0x211a, 0xc0
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x7c00, 41
    ; Mode 3's 8bpp BG1 spans all four plane-pairs and supports direct color.
    write_ppu 0x2105, 3
    write_ppu 0x210d, 0
    write_ppu 0x210d, 0
    write_ppu 0x2107, 0x10
    write_ppu 0x210e, 0xff
    write_ppu 0x210e, 0xff
    mov word [ppu_vram + 0x2000], 0x1c00
    mov word [ppu_vram + 0], 0x0080
    mov word [ppu_vram + 16], 0x8000
    mov word [ppu_vram + 32], 0
    mov word [ppu_vram + 48], 0x8000
    mov word [ppu_cgram + 0x89 * 2], 0x7fff
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x7fff, 42
    write_ppu 0x2130, 1
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0x50c6, 43
    ; PPU H/V latch and status report counter, field and PAL values.
    mov dword [hcounter], 1320
    mov dword [vcounter], 240
    mov dword [line_clocks], 1364
    mov dword [frame_lines], 312
    mov byte [field_odd], 1
    mov byte [io_port], 0x80
    mov eax, 0x2137
    call ppu_read
    mov eax, 0x213c
    call ppu_read
    fail_if_ne eax, 73, 44
    mov eax, 0x213c
    call ppu_read
    fail_if_ne eax, 73, 45
    mov eax, 0x213f
    call ppu_read
    fail_if_ne eax, 0xd3, 46
    mov eax, 0x213f
    call ppu_read
    fail_if_ne eax, 0x93, 47
    ; Nine large sprites consume the 34-tile budget in reverse OAM order.
    ; The first small sprite disappears while a later large sprite remains.
    write_ppu 0x2105, 0
    write_ppu 0x2101, 0x21
    write_ppu 0x212c, 0x10
    write_ppu 0x2130, 0
    lea edi, [ppu_oam + 4]
    mov ecx, 9
    mov eax, 0x00000020
    rep stosd
    mov dword [ppu_oam], 0
    mov word [ppu_vram + 0x4000], 0x0080
    mov byte [ppu_oam + 0x200], 0xa8
    mov byte [ppu_oam + 0x201], 0xaa
    mov byte [ppu_oam + 0x202], 0x0a
    mov eax, 1
    call ppu_scanline
    fail_if_ne word [video_pixels], 0, 48
    fail_if_ne word [video_pixels + 64], 0x7c00, 49
    fail_if_ne byte [ppu_obj_status], 0x80, 50

    xor edi, edi
exit:
    mov eax, SYS_EXIT
    syscall
%include "ppu.inc"
align PAGE_SIZE, db 0
data_start:
    dd 0
align PAGE_SIZE, db 0
data_end:
absolute data_end
    hcounter: resd 1
    vcounter: resd 1
    cpu_mdr: resb 1
    io_port: resb 1
    line_clocks: resd 1
    field_odd: resb 1
    frame_lines: resd 1
    PPU_STATE
data_memory_size equ $ - data_start
