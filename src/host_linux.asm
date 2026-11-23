; Flat x86-64 executable. The first mapping holds headers and code, and the
; second holds writable state. Both mappings fit 32-bit absolute addresses.
bits 64
default abs
[warning -reloc-abs-dword]
[warning -reloc-abs-qword]
IMAGE_BASE equ 0x400000
PAGE_SIZE equ 4096
ROM_CAP equ 8389120
FRAME_PIXELS equ 256 * 224
DEFAULT_STEPS equ 10000000
%ifndef TRACE_EVENTS
    %define TRACE_EVENTS 65536
%endif
org IMAGE_BASE

%ifdef HOST_MAC
    SYS_READ   equ 0x2000003
    SYS_WRITE  equ 0x2000004
    SYS_OPEN   equ 0x2000005
    SYS_CLOSE  equ 0x2000006
    SYS_EXIT   equ 0x2000001
    FILE_OUT_FLAGS equ 0x0601
%else
    SYS_READ   equ 0
    SYS_WRITE  equ 1
    SYS_OPEN   equ 2
    SYS_CLOSE  equ 3
    SYS_EXIT   equ 60
    FILE_OUT_FLAGS equ 0x0241
%endif

%macro HOST_CALL 1
    mov eax, %1
%ifdef HOST_MAC
    push rdx
%endif
    syscall
%ifdef HOST_MAC
    pop rdx
    jnc %%success
    neg rax
%%success:
%endif
%endmacro

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

start:
    cld
    mov rbp, rsp
    and rsp, -16
    mov rcx, [rbp]
    cmp rcx, 2
    jb usage_error
    cmp rcx, 5
    ja usage_error
    mov rdi, [rbp + 16]
    mov [input_path], rdi
    mov dword [steps_left], DEFAULT_STEPS
    cmp ecx, 2
    je .read_rom
    mov rsi, [rbp + 24]
    cmp ecx, 3
    je .steps
    mov rax, [rbp + 32]
    cmp byte [rax], '-'
    jne .frame_path
    cmp byte [rax + 1], 0
    je .maybe_trace
.frame_path:
    mov [output_path], rax
.maybe_trace:
    cmp ecx, 5
    jne .steps
%if TRACE_EVENTS
    mov rax, [rbp + 40]
    mov [trace_path], rax
%else
    jmp usage_error
%endif
.steps:
    call parse_steps
    test eax, eax
    jz usage_error
    mov [steps_left], eax
.read_rom:
    mov rdi, [input_path]
    xor esi, esi
    xor edx, edx
    HOST_CALL SYS_OPEN
    test rax, rax
    js input_error
    mov r12, rax
    mov esi, rom_buffer
    mov edx, ROM_CAP + 1
    mov rdi, r12
.read_next:
    HOST_CALL SYS_READ
    cmp rax, -4
    je .read_next
    test rax, rax
    js .read_failed
    jz .read_end
    add rsi, rax
    sub rdx, rax
    jnz .read_next
.read_failed:
    mov rdi, r12
    HOST_CALL SYS_CLOSE
    jmp input_error
.read_end:
    sub esi, rom_buffer
    mov [rom_size], esi
    mov rdi, r12
    HOST_CALL SYS_CLOSE
%if TRACE_EVENTS
    cmp qword [trace_path], 0
    setne byte [trace_capture]
%endif
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
    je .trace
    call dump_frame
    test eax, eax
    jnz output_error
.trace:
%if TRACE_EVENTS
    cmp qword [trace_path], 0
    je .report
    call dump_trace
    test eax, eax
    jnz output_error
%endif
.report:
    mov eax, [tick_hi]
    mov edi, summary_tick_hi
    call hex32
    mov eax, [tick_lo]
    mov edi, summary_tick_lo
    call hex32
    mov eax, [bus_count]
    mov edi, summary_bus
    call hex32
    mov eax, [trace_hash]
    mov edi, summary_hash
    call hex32
    mov eax, [video_frame]
    mov edi, summary_frames
    call hex32
    mov eax, [wram + 0x1ff0]
    bswap eax
    mov edi, summary_wram
    call hex32
    mov eax, [cpu_fault]
    mov edi, summary_fault
    call hex32
    mov eax, [run_status]
    mov edi, summary_status
    call hex32
    mov edi, 1
    mov esi, summary
    mov edx, summary_end - summary
    call write_all
    test eax, eax
    jnz output_error
    xor edi, edi
    mov eax, [run_status]
    cmp eax, 1
    je exit_program
    lea edi, [eax + 1]
    jmp exit_program

usage_error:
    mov esi, usage_msg
    mov edx, usage_end - usage_msg
    jmp bad_input
input_error:
    mov esi, input_msg
    mov edx, input_end - input_msg
bad_input:
    mov edi, 2
    call write_all
    mov edi, 2
    jmp exit_program
output_error:
    mov edi, 2
    mov esi, output_msg
    mov edx, output_end - output_msg
    call write_all
    mov edi, 5
exit_program:
    HOST_CALL SYS_EXIT
    ud2

; RSI points to a decimal string. Return a nonzero uint32 or zero for invalid input.
parse_steps:
    xor edx, edx
.next:
    movzx ecx, byte [rsi]
    inc rsi
    test ecx, ecx
    jz .done
    sub ecx, '0'
    cmp ecx, 9
    ja .invalid
    cmp edx, 429496729
    ja .invalid
    jb .append
    cmp ecx, 5
    ja .invalid
.append:
    imul edx, edx, 10
    add edx, ecx
    jmp .next
.done:
    mov eax, edx
    ret
.invalid:
    xor eax, eax
    ret

; EAX is the value to format and RDI points to eight writable bytes.
hex32:
    mov ecx, 8
.next:
    rol eax, 4
    mov edx, eax
    and edx, 15
    mov dl, [hex_chars + rdx]
    mov [rdi], dl
    inc rdi
    dec ecx
    jnz .next
    ret

; Write RDX bytes at RSI to RDI. Return zero on success.
write_all:
    test rdx, rdx
    jz .done
.next:
    HOST_CALL SYS_WRITE
    cmp rax, -4
    je .next
    test rax, rax
    jle .error
    add rsi, rax
    sub rdx, rax
    jnz .next
.done:
    xor eax, eax
    ret
.error:
    mov eax, 1
    ret

; Expand canonical BGR555 pixels into an RGB PPM carried by the host only.
dump_frame:
    mov esi, video_pixels
    mov edi, frame_rgb
    mov ecx, FRAME_PIXELS
.next_pixel:
    lodsw
    movzx edx, ax
%rep 3
    mov eax, edx
    and eax, 31
    mov r8d, eax
    shl eax, 3
    shr r8d, 2
    or eax, r8d
    stosb
    shr edx, 5
%endrep
    dec ecx
    jnz .next_pixel

    mov rdi, [output_path]
    mov esi, FILE_OUT_FLAGS
    mov edx, 0o666
    HOST_CALL SYS_OPEN
    test rax, rax
    js .open_failed
    mov r12, rax
    mov rdi, r12
    mov esi, ppm_header
    mov edx, ppm_header_end - ppm_header
    call write_all
    test eax, eax
    jnz .close
    mov rdi, r12
    mov esi, frame_rgb
    mov edx, FRAME_PIXELS * 3
    call write_all
.close:
    mov r13d, eax
    mov rdi, r12
    HOST_CALL SYS_CLOSE
    test rax, rax
    js .open_failed
    mov eax, r13d
    ret
.open_failed:
    mov eax, 1
    ret

%if TRACE_EVENTS
dump_trace:
    mov rdi, [trace_path]
    mov esi, FILE_OUT_FLAGS
    mov edx, 0o666
    HOST_CALL SYS_OPEN
    test rax, rax
    js .open_failed
    mov r12, rax
    mov rdi, r12
    mov esi, trace_data
    mov edx, [trace_used]
    shl edx, 4
    call write_all
    mov r13d, eax
    mov rdi, r12
    HOST_CALL SYS_CLOSE
    test rax, rax
    js .open_failed
    mov eax, r13d
    ret
.open_failed:
    mov eax, 1
    ret
%endif

%include "core.inc"

hex_chars: db "0123456789abcdef"
usage_msg: db "Usage: runner ROM [max-instructions] [frame.ppm|-] [trace.bin]", 10
usage_end:
input_msg: db "Unable to read ROM or detect its mapping.", 10
input_end:
output_msg: db "Unable to write output.", 10
output_end:
ppm_header: db "P6", 10, "256 224", 10, "255", 10
ppm_header_end:

align PAGE_SIZE, db 0
data_start:
summary: db "ticks="
summary_tick_hi: db "00000000:"
summary_tick_lo: db "00000000 bus="
summary_bus: db "00000000 hash="
summary_hash: db "00000000 frames="
summary_frames: db "00000000 wram="
summary_wram: db "00000000 fault="
summary_fault: db "00000000 status="
summary_status: db "00000000", 10
summary_end:
align PAGE_SIZE, db 0
data_end:

absolute data_end
input_path: resq 1
trace_path: resq 1
output_path: resq 1
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
