; DOS MZ runner for a 386 and a DPMI 0.9/1.0 host with 32-bit support.
; All DOS file I/O passes through a conventional-memory transfer buffer.
cpu 386
bits 16
org 0
[warning -reloc-abs-word]
[warning -reloc-abs-dword]
ROM_CAP equ 8389120
BOUNCE_SIZE equ 8192
DEFAULT_STEPS equ 10000000
REAL_STACK_PARAS equ 0x100

file_start:
    db "MZ"
    dw (file_end - file_start) & 511
    dw (file_end - file_start + 511) / 512
    dw 0, (disk_image - file_start) / 16
    dw REAL_STACK_PARAS, REAL_STACK_PARAS
    dw 0, ((file_end - disk_image + 15) & -16) + REAL_STACK_PARAS * 16
    dw 0, boot_entry - disk_image, 0, 0x1c, 0
    times 32 - ($ - $$) db 0

disk_image:
boot_entry:
    cld
    mov ax, ds
    mov fs, ax
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov [real_segment - disk_image], ax
    movzx cx, byte [fs:0x80]
    mov si, 0x81
    mov di, command_tail - disk_image
    fs rep movsb
    xor ax, ax
    stosb
    mov fs, ax
    mov si, command_tail - disk_image
    call skip_spaces
    cmp byte [si], 0
    je boot_usage
    mov [path_offset - disk_image], si
.path_character:
    lodsb
    test al, al
    jz boot_open_rom
    cmp al, ' '
    je .path_end
    cmp al, 9
    jne .path_character
.path_end:
    mov byte [si - 1], 0
    call skip_spaces
    cmp byte [si], 0
    je boot_open_rom
    xor edx, edx
.step_digit:
    movzx ecx, byte [si]
    inc si
    sub ecx, '0'
    cmp ecx, 9
    ja .end_steps
    cmp edx, 429496729
    ja boot_usage
    jb .append_digit
    cmp ecx, 5
    ja boot_usage
.append_digit:
    imul edx, edx, 10
    add edx, ecx
    jmp .step_digit
.end_steps:
    dec si
    call skip_spaces
    cmp byte [si], 0
    jne boot_usage
    test edx, edx
    jz boot_usage
    mov [selected_steps - disk_image], edx
    jmp boot_open_rom

skip_spaces:
    cmp byte [si], ' '
    je .skip
    cmp byte [si], 9
    jne .done
.skip:
    inc si
    jmp skip_spaces
.done:
    ret

boot_open_rom:
    mov dx, [path_offset - disk_image]
    mov ax, 0x3d00
    int 0x21
    jc boot_bad_rom
    mov [file_handle - disk_image], ax
    mov bx, ax
    xor dx, dx
    xor cx, cx
    mov ax, 0x4202
    int 0x21
    jc boot_bad_rom
    movzx eax, ax
    movzx edx, dx
    shl edx, 16
    or eax, edx
    test eax, eax
    jz boot_bad_rom
    cmp eax, ROM_CAP
    ja boot_bad_rom
    mov [file_size - disk_image], eax
    add eax, rom_buffer + 4095
    and eax, -4096
    mov [arena_size - disk_image], eax

    mov bx, [file_handle - disk_image]
    xor dx, dx
    xor cx, cx
    mov ax, 0x4200
    int 0x21
    jc boot_bad_rom

    mov ax, 0x1687
    int 0x2f
    test ax, ax
    jnz boot_no_dpmi
    test bx, 1
    jz boot_no_dpmi
    mov [dpmi_pointer - disk_image], di
    mov [dpmi_pointer - disk_image + 2], es
    test si, si
    jz .mode_switch
    mov bx, si
    mov ah, 0x48
    int 0x21
    jc boot_no_dpmi
    mov es, ax
.mode_switch:
    mov ax, 1
    call far [dpmi_pointer - disk_image]
    jc boot_no_dpmi

    ; The DPMI entry returns into a 16-bit code segment. Build separate
    ; 32-bit code and data selectors covering the same allocated arena.
    mov esi, [arena_size - disk_image]
    mov ecx, esi
    mov ebx, esi
    shr ebx, 16
    mov ax, 0x0501
    int 0x31
    jc boot_pm_error
    mov [arena_base_hi - disk_image], bx
    mov [arena_base_lo - disk_image], cx

    xor ax, ax
    mov cx, 2
    int 0x31
    jc boot_pm_error
    mov [arena_data - disk_image], ax
    mov ax, 3
    int 0x31
    add ax, [arena_data - disk_image]
    mov [entry32_pointer - disk_image + 4], ax

    mov bx, [arena_data - disk_image]
    mov cx, cs
    and cx, 3
    shl cx, 5
    or cx, 0x4092
    call initialize_selector
    jc boot_pm_error
    mov bx, [entry32_pointer - disk_image + 4]
    mov cx, cs
    and cx, 3
    shl cx, 5
    or cx, 0x409a
    call initialize_selector
    jc boot_pm_error

    mov es, [arena_data - disk_image]
    xor si, si
    mov di, disk_image
    mov cx, file_end - disk_image
    rep movsb

    mov ax, ds
    mov fs, ax
    mov ax, es
    mov ds, ax
    mov ss, ax
    mov esp, stack_top
    jmp dword far [entry32_pointer]

; BX = selector, CX = requested access rights. Keep the G bit managed by DPMI.
initialize_selector:
    mov ax, 0x0009
    int 0x31
    jc .done
    mov cx, [arena_base_hi - disk_image]
    mov dx, [arena_base_lo - disk_image]
    mov ax, 0x0007
    int 0x31
    jc .done
    mov edx, [arena_size - disk_image]
    dec edx
    mov ecx, edx
    shr ecx, 16
    mov ax, 0x0008
    int 0x31
.done:
    ret

boot_usage:
    mov dx, usage_message - disk_image
    jmp boot_error
boot_bad_rom:
    mov dx, bad_rom_message - disk_image
    jmp boot_error
boot_no_dpmi:
    mov dx, no_dpmi_message - disk_image
boot_error:
    mov ah, 9
    int 0x21
    jmp boot_exit

boot_pm_error:
    ; Ask DPMI to print the message from its real-mode address.
    mov ax, ds
    mov es, ax
    mov ax, [real_segment - disk_image]
    mov [pm_error_ds - disk_image], ax
    mov edi, pm_error_regs - disk_image
    mov ax, 0x0300
    mov bx, 0x21
    xor cx, cx
    int 0x31
boot_exit:
    mov ax, 0x4c02
    int 0x21

real_segment: dw 0
path_offset: dw 0
file_handle: dw 0
arena_base_hi: dw 0
arena_base_lo: dw 0
arena_data: dw 0
dpmi_pointer: dd 0
entry32_pointer: dd dos_entry
    dw 0
file_size: dd 0
arena_size: dd 0
selected_steps: dd DEFAULT_STEPS
command_tail: times 128 db 0
pm_error_regs: dd 0, 0, 0, 0, 0, pm_error_message - disk_image, 0, 0x0900
    dw 0, 0
pm_error_ds: dw 0
    times 6 dw 0
usage_message: db "Usage: SNESDOS ROM [max-instructions]", 13, 10, '$'
bad_rom_message: db "Unable to read ROM.", 13, 10, '$'
no_dpmi_message: db "A 32-bit DPMI host is required.", 13, 10, '$'
pm_error_message: db "Unable to allocate emulator memory through DPMI.", 13, 10, '$'

bits 32
dos_entry:
    xor eax, eax
    mov edi, state_start
    mov ecx, (state_end - state_start) / 4
    rep stosd
    mov eax, [file_size]
    mov [rom_size], eax
    mov eax, [selected_steps]
    mov [steps_left], eax
    call read_rom
    jc .bad_rom
    call core_reset
    test eax, eax
    jnz .bad_rom
.run:
    call core_step
    test eax, eax
    jnz .finished
    dec dword [steps_left]
    jnz .run
    mov eax, 3
.finished:
    mov [run_status], eax
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
    xchg al, ah
    rol eax, 16
    xchg al, ah
    mov edi, summary_wram
    call hex32
    mov eax, [cpu_fault]
    mov edi, summary_fault
    call hex32
    mov eax, [run_status]
    mov edi, summary_status
    call hex32
    mov esi, summary
    mov ecx, summary_end - summary
    mov bx, 1
    call dos_write
    jc .write_failed
    xor eax, eax
    mov ecx, [run_status]
    cmp ecx, 1
    je .exit
    lea eax, [ecx + 1]
    jmp .exit
.bad_rom:
    mov esi, mapping_error
    mov ecx, mapping_error_end - mapping_error
    mov bx, 2
    call dos_write
    mov eax, 2
    jmp .exit
.write_failed:
    mov eax, 5
.exit:
    mov ah, 0x4c
    int 0x21

read_rom:
    mov ebp, [rom_size]
    mov edi, rom_buffer
.next:
    test ebp, ebp
    jz .done
    mov ecx, ebp
    cmp ecx, BOUNCE_SIZE
    jbe .request
    mov ecx, BOUNCE_SIZE
.request:
    push ecx
    push edi
    call clear_registers
    mov [rm_ecx], ecx
    mov dword [rm_eax], 0x3f00
    movzx eax, word [file_handle]
    mov [rm_ebx], eax
    call dos_call
    pop edi
    pop ecx
    jc .error
    movzx eax, ax
    test eax, eax
    jz .error
    cmp eax, ecx
    ja .error
    sub ebp, eax
    mov ecx, eax
    mov esi, bounce_buffer - disk_image
    fs rep movsb
    jmp .next
.done:
    clc
    ret
.error:
    stc
    ret

; Copy ECX bytes at ESI to the real DOS handle BX.
dos_write:
    push ecx
    push es
    mov ax, fs
    mov es, ax
    mov edi, bounce_buffer - disk_image
    rep movsb
    pop es
    pop ecx
    call clear_registers
    movzx eax, bx
    mov [rm_ebx], eax
    mov [rm_ecx], ecx
    mov dword [rm_eax], 0x4000
    push ecx
    call dos_call
    pop ecx
    jc .failed
    cmp ax, cx
    jne .failed
    clc
    ret
.failed:
    stc
    ret

clear_registers:
    pushad
    xor eax, eax
    mov edi, rm_regs
    mov ecx, (rm_regs_end - rm_regs) / 4
    rep stosd
    mov ax, [real_segment]
    mov [rm_ds], ax
    mov dword [rm_edx], bounce_buffer - disk_image
    popad
    ret

dos_call:
    mov edi, rm_regs
    mov ax, 0x0300
    mov bx, 0x21
    xor ecx, ecx
    int 0x31
    jc .failed
    mov eax, [rm_eax]
    test byte [rm_flags], 1
    jnz .failed
    ret
.failed:
    stc
    ret

hex32:
    mov ecx, 8
.next:
    rol eax, 4
    mov edx, eax
    and edx, 15
    mov dl, [hex_chars + edx]
    mov [edi], dl
    inc edi
    dec ecx
    jnz .next
    ret

%include "core.inc"

hex_chars: db "0123456789abcdef"
mapping_error: db "Unable to read ROM or detect its mapping.", 13, 10
mapping_error_end:
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
bounce_buffer: times BOUNCE_SIZE db 0
align 16, db 0
file_end:
times -((file_end - disk_image + REAL_STACK_PARAS*16) > 65535) db 0

absolute file_end
state_start:
steps_left: resd 1
run_status: resd 1
rm_regs:
    resd 4
rm_ebx: resd 1
rm_edx: resd 1
rm_ecx: resd 1
rm_eax: resd 1
rm_flags: resw 1
    resw 1
rm_ds: resw 1
    resw 7
rm_regs_end:
CORE_STATE
resb (-($ - state_start)) & 3
state_end:
resb 8192
stack_top:
rom_buffer:
