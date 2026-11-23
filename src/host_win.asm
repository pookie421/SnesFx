; Standalone PE32+ console runner. The emulator and launcher are assembled
; together, with Win32 API imports for files, command-line parsing and exit.
bits 64
default abs
[warning -reloc-abs-dword]
[warning -reloc-abs-qword]
IMAGE_BASE equ 0x400000
PAGE_SIZE equ 4096
ROM_CAP equ 8389120
FRAME_PIXELS equ 256 * 224
DEFAULT_STEPS equ 10000000
org IMAGE_BASE

file_start:
    db "MZ"
    dw 0x80, 1, 0, 4, 0, 0xffff, 0, 0xb8
    dw 0, 0, 0, 0x40, 0
    times 0x3c - ($ - $$) db 0
    dd pe_header - file_start
    mov ax, 0x4c02
    int 0x21
    times 0x80 - ($ - $$) db 0

pe_header:
    db "PE", 0, 0
    dw 0x8664, 2
    dd 0, 0, 0
    dw optional_end - optional_header, 0x23
optional_header:
    dw 0x020b
    db 1, 0
    dd data_start - text_start, data_end - data_start
    dd data_memory_size - (data_end - data_start)
    dd start - IMAGE_BASE, text_start - IMAGE_BASE
    dq IMAGE_BASE
    dd PAGE_SIZE, 512
    dw 6, 0, 0, 0, 6, 0
    dd 0, data_start - IMAGE_BASE + data_memory_size, text_start - file_start, 0
    dw 3, 0x0100
    dq 0x100000, 0x1000, 0x100000, 0x1000
    dd 0, 16
    dq 0
    dd import_table - IMAGE_BASE, import_table_end - import_table
    dq 0
    dd unwind_table - IMAGE_BASE, unwind_table_end - unwind_table
    times 8 dq 0
    dd iat - IMAGE_BASE, iat_end - iat
    times 3 dq 0
optional_end:
    db ".text", 0, 0, 0
    dd text_end - text_start, text_start - IMAGE_BASE
    dd data_start - text_start, text_start - file_start
    dd 0, 0, 0, 0x60000020
    db ".data", 0, 0, 0
    dd data_memory_size, data_start - IMAGE_BASE
    dd data_end - data_start, data_start - file_start
    dd 0, 0, 0, 0xc0000040

align PAGE_SIZE, db 0
text_start:
start:
    sub rsp, 56
    cld
    call [win_GetCommandLineW]
    mov rcx, rax
    mov edx, argc
    call [win_CommandLineToArgvW]
    test rax, rax
    jz usage_error
    mov edx, [argc]
    cmp edx, 2
    jb usage_error
    cmp edx, 4
    ja usage_error
    mov rcx, [rax + 8]
    mov [input_path], rcx
    mov dword [steps_left], DEFAULT_STEPS
    cmp edx, 2
    je .open_rom
    mov rcx, [rax + 16]
    cmp edx, 3
    je .steps
    mov rdx, [rax + 24]
    mov [output_path], rdx
.steps:
    call parse_steps
    test eax, eax
    jz usage_error
    mov [steps_left], eax
.open_rom:
    mov rcx, [input_path]
    mov edx, 0x80000000
    mov r8d, 1
    xor r9d, r9d
    mov qword [rsp + 32], 3
    mov qword [rsp + 40], 0x80
    mov qword [rsp + 48], 0
    call [win_CreateFileW]
    cmp rax, -1
    je input_error
    mov [input_handle], rax
    mov dword [input_cursor], rom_buffer
    mov dword [input_space], ROM_CAP + 1
.next_read:
    mov rcx, [input_handle]
    mov edx, [input_cursor]
    mov r8d, [input_space]
    mov r9d, transfer_bytes
    mov qword [rsp + 32], 0
    call [win_ReadFile]
    test eax, eax
    jz .read_failed
    mov eax, [transfer_bytes]
    test eax, eax
    jz .read_complete
    add [input_cursor], eax
    sub [input_space], eax
    jnz .next_read
.read_failed:
    mov rcx, [input_handle]
    call [win_CloseHandle]
    jmp input_error
.read_complete:
    mov eax, [input_cursor]
    sub eax, rom_buffer
    mov [rom_size], eax
    mov rcx, [input_handle]
    call [win_CloseHandle]
    call core_reset
    test eax, eax
    jnz input_error
.run:
    call core_step
    test eax, eax
    jnz .finished
    dec dword [steps_left]
    jnz .run
    mov eax, 3
.finished:
    mov [run_status], eax
    cmp qword [output_path], 0
    je .report
    call dump_frame
    test eax, eax
    jnz output_error

.report:
    mov eax, [tick_hi]
    mov r9d, summary_tick_hi
    call hex32
    mov eax, [tick_lo]
    mov r9d, summary_tick_lo
    call hex32
    mov eax, [bus_count]
    mov r9d, summary_bus
    call hex32
    mov eax, [trace_hash]
    mov r9d, summary_hash
    call hex32
    mov eax, [video_frame]
    mov r9d, summary_frames
    call hex32
    mov eax, [wram + 0x1ff0]
    bswap eax
    mov r9d, summary_wram
    call hex32
    mov eax, [cpu_fault]
    mov r9d, summary_fault
    call hex32
    mov eax, [run_status]
    mov r9d, summary_status
    call hex32

    mov ecx, -11
    call [win_GetStdHandle]
    mov rcx, rax
    mov edx, summary
    mov r8d, summary_end - summary
    call write_all
    test eax, eax
    jnz output_error
    xor ecx, ecx
    mov eax, [run_status]
    cmp eax, 1
    je exit_program
    lea ecx, [eax + 1]
    jmp exit_program

usage_error:
    mov edx, usage_msg
    mov r8d, usage_end - usage_msg
    jmp bad_input
input_error:
    mov edx, input_msg
    mov r8d, input_end - input_msg
bad_input:
    call print_error
    mov ecx, 2
    jmp exit_program
output_error:
    mov edx, output_msg
    mov r8d, output_end - output_msg
    call print_error
    mov ecx, 5
exit_program:
    call [win_ExitProcess]
    ud2
start_end:

; RCX points to a decimal UTF-16 argument. Return a nonzero uint32 or zero.
parse_steps:
    xor edx, edx
.next:
    movzx r8d, word [rcx]
    add rcx, 2
    test r8d, r8d
    jz .done
    sub r8d, '0'
    cmp r8d, 9
    ja .invalid
    cmp edx, 429496729
    ja .invalid
    jb .append
    cmp r8d, 5
    ja .invalid
.append:
    imul edx, edx, 10
    add edx, r8d
    jmp .next
.done:
    mov eax, edx
    ret
.invalid:
    xor eax, eax
    ret

; EAX is the value to format and R9 points to eight writable bytes.
hex32:
    mov ecx, 8
.next:
    rol eax, 4
    mov edx, eax
    and edx, 15
    mov dl, [hex_chars + rdx]
    mov [r9], dl
    inc r9
    dec ecx
    jnz .next
    ret

print_error:
    sub rsp, 56
    mov [rsp + 32], rdx
    mov [rsp + 40], r8d
    mov ecx, -12
    call [win_GetStdHandle]
    mov rcx, rax
    mov rdx, [rsp + 32]
    mov r8d, [rsp + 40]
    call write_all
    add rsp, 56
    ret
print_error_end:

; RCX is the handle, RDX the byte buffer, and R8D the remaining length.
write_all:
    sub rsp, 72
    mov [rsp + 40], rcx
    mov [rsp + 48], rdx
    mov [rsp + 56], r8d
.next:
    mov rcx, [rsp + 40]
    mov rdx, [rsp + 48]
    mov r8d, [rsp + 56]
    mov r9d, transfer_bytes
    mov qword [rsp + 32], 0
    call [win_WriteFile]
    test eax, eax
    jz .error
    mov eax, [transfer_bytes]
    test eax, eax
    jz .error
    add [rsp + 48], rax
    sub [rsp + 56], eax
    jnz .next
    xor eax, eax
    jmp .return
.error:
    mov eax, 1
.return:
    add rsp, 72
    ret
write_all_end:

dump_frame:
    sub rsp, 72
    mov r10d, video_pixels
    mov r11d, frame_rgb
    mov ecx, FRAME_PIXELS
.next_pixel:
    movzx edx, word [r10]
    add r10, 2
%rep 3
    mov eax, edx
    and eax, 31
    mov r8d, eax
    shl eax, 3
    shr r8d, 2
    or eax, r8d
    mov [r11], al
    inc r11
    shr edx, 5
%endrep
    dec ecx
    jnz .next_pixel

    mov rcx, [output_path]
    mov edx, 0x40000000
    xor r8d, r8d
    xor r9d, r9d
    mov qword [rsp + 32], 2
    mov qword [rsp + 40], 0x80
    mov qword [rsp + 48], 0
    call [win_CreateFileW]
    cmp rax, -1
    je .failed
    mov [rsp + 56], rax
    mov rcx, rax
    mov edx, ppm_header
    mov r8d, ppm_header_end - ppm_header
    call write_all
    test eax, eax
    jnz .close
    mov rcx, [rsp + 56]
    mov edx, frame_rgb
    mov r8d, FRAME_PIXELS * 3
    call write_all
.close:
    mov [rsp + 64], eax
    mov rcx, [rsp + 56]
    call [win_CloseHandle]
    test eax, eax
    jz .failed
    mov eax, [rsp + 64]
    jmp .return
.failed:
    mov eax, 1
.return:
    add rsp, 72
    ret
dump_frame_end:

%include "core.inc"

hex_chars: db "0123456789abcdef"
usage_msg: db "Usage: runner ROM [max-instructions] [frame.ppm]", 13, 10
usage_end:
input_msg: db "Unable to read ROM or detect its mapping.", 13, 10
input_end:
output_msg: db "Unable to write output.", 13, 10
output_end:
ppm_header: db "P6", 10, "256 224", 10, "255", 10
ppm_header_end:
text_end:

align PAGE_SIZE, db 0
data_start:
import_table:
    dd kernel_ilt - IMAGE_BASE, 0, 0, kernel_name - IMAGE_BASE, kernel_iat - IMAGE_BASE
    dd shell_ilt - IMAGE_BASE, 0, 0, shell_name - IMAGE_BASE, shell_iat - IMAGE_BASE
    times 5 dd 0
import_table_end:
align 8, db 0
kernel_ilt:
    dq name_GetCommandLineW - IMAGE_BASE, name_CreateFileW - IMAGE_BASE
    dq name_ReadFile - IMAGE_BASE, name_WriteFile - IMAGE_BASE
    dq name_CloseHandle - IMAGE_BASE, name_GetStdHandle - IMAGE_BASE
    dq name_ExitProcess - IMAGE_BASE, 0
shell_ilt:
    dq name_CommandLineToArgvW - IMAGE_BASE, 0

iat:
kernel_iat:
win_GetCommandLineW: dq name_GetCommandLineW - IMAGE_BASE
win_CreateFileW: dq name_CreateFileW - IMAGE_BASE
win_ReadFile: dq name_ReadFile - IMAGE_BASE
win_WriteFile: dq name_WriteFile - IMAGE_BASE
win_CloseHandle: dq name_CloseHandle - IMAGE_BASE
win_GetStdHandle: dq name_GetStdHandle - IMAGE_BASE
win_ExitProcess: dq name_ExitProcess - IMAGE_BASE
    dq 0
shell_iat:
win_CommandLineToArgvW: dq name_CommandLineToArgvW - IMAGE_BASE
    dq 0
iat_end:

%macro IMPORT_NAME 1
    align 2, db 0
    name_%1: dw 0
    db %str(%1), 0
%endmacro
IMPORT_NAME GetCommandLineW
IMPORT_NAME CreateFileW
IMPORT_NAME ReadFile
IMPORT_NAME WriteFile
IMPORT_NAME CloseHandle
IMPORT_NAME GetStdHandle
IMPORT_NAME ExitProcess
IMPORT_NAME CommandLineToArgvW
kernel_name: db "kernel32.dll", 0
shell_name: db "shell32.dll", 0

align 4, db 0
unwind_table:
    dd start - IMAGE_BASE, start_end - IMAGE_BASE, unwind_alloc56 - IMAGE_BASE
    dd print_error - IMAGE_BASE, print_error_end - IMAGE_BASE, unwind_alloc56 - IMAGE_BASE
    dd write_all - IMAGE_BASE, write_all_end - IMAGE_BASE, unwind_alloc72 - IMAGE_BASE
    dd dump_frame - IMAGE_BASE, dump_frame_end - IMAGE_BASE, unwind_alloc72 - IMAGE_BASE
unwind_table_end:
unwind_alloc56: db 1, 4, 1, 0, 4, 0x62, 0, 0
unwind_alloc72: db 1, 4, 1, 0, 4, 0x82, 0, 0

summary: db "ticks="
summary_tick_hi: db "00000000:"
summary_tick_lo: db "00000000 bus="
summary_bus: db "00000000 hash="
summary_hash: db "00000000 frames="
summary_frames: db "00000000 wram="
summary_wram: db "00000000 fault="
summary_fault: db "00000000 status="
summary_status: db "00000000", 13, 10
summary_end:
align PAGE_SIZE, db 0
data_end:

absolute data_end
input_path: resq 1
output_path: resq 1
input_handle: resq 1
argc: resd 1
input_cursor: resd 1
input_space: resd 1
transfer_bytes: resd 1
steps_left: resd 1
run_status: resd 1
resb (-($ - data_end)) & 63
CORE_STATE
resb (-($ - data_end)) & 63
rom_buffer: resb ROM_CAP + 1
resb (-($ - data_end)) & 63
frame_rgb: resb FRAME_PIXELS * 3
memory_end:
data_memory_size equ (memory_end - data_start + PAGE_SIZE - 1) & -PAGE_SIZE
