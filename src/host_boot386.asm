; 1.44 MiB bootable floppy for a 386 with 2 MiB of RAM and a VGA adapter.
; The embedded ROM is loaded with the payload through the PC BIOS.
cpu 386
bits 16
[warning -reloc-abs-word]
[warning -reloc-abs-dword]

%ifndef ROM_FILE
    %error "Define ROM_FILE as a quoted path to the ROM to embed"
%endif
%ifndef STEPS
    %define STEPS 10000000
%endif
%if STEPS < 1 || STEPS > 0xffffffff
    %error "STEPS must be an integer in the range 1..4294967295"
%endif

ROM_CAP equ 8389120
FLOPPY_BYTES equ 1440 * 1024
FIRST_PAYLOAD_SEGMENT equ 0x1000
PAYLOAD_ADDRESS equ 0x10000
STATE_ADDRESS equ 0x100000
CODE_SELECTOR equ 8
DATA_SELECTOR equ 16

section .boot start=0 vstart=0x7c00
    jmp 0:boot_entry
boot_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7c00
    cld
    sti
    mov [boot_drive], dl
    mov ax, 3
    int 0x10

    mov ax, FIRST_PAYLOAD_SEGMENT
    mov es, ax
    mov si, 1
    mov bp, (payload_disk_end - payload_entry) / 512
.next_sector:
    mov di, 3
.retry_sector:
    pusha
    push es
    mov ax, si
    xor dx, dx
    div word [sectors_per_track]
    mov cl, dl
    inc cl
    xor dx, dx
    div word [head_count]
    mov ch, al
    mov dh, dl
    mov dl, [boot_drive]
    xor bx, bx
    mov ax, 0x0201
    int 0x13
    pop es
    popa
    jnc .read_success
    pusha
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    popa
    dec di
    jnz .retry_sector

    mov si, disk_error_msg
    jmp bios_error
.read_success:
    inc si
    mov ax, es
    add ax, 0x20
    mov es, ax
    dec bp
    jnz .next_sector

    ; Try the BIOS A20 service and the later system control port first.
    mov ax, 0x2401
    int 0x15
    cli
    in al, 0x92
    or al, 2
    and al, 0xfe
    out 0x92, al
    call test_a20
    jnz .enter_protected_mode

    ; AT-style machines may implement the gate at the keyboard controller.
    call wait_8042
    jc .a20_failed
    mov al, 0xd1
    out 0x64, al
    call wait_8042
    jc .a20_failed
    mov al, 0xdf
    out 0x60, al
    call wait_8042
    call test_a20
    jnz .enter_protected_mode
.a20_failed:
    mov si, a20_error_msg
    jmp bios_error
.enter_protected_mode:
    lgdt [boot_gdt_pointer]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SELECTOR:payload_entry

; ZF=1 if the two addresses one MiB apart alias each other.
test_a20:
    push es
    mov ax, 0xffff
    mov es, ax
    mov al, [0x0500]
    mov ah, [es:0x0510]
    mov byte [0x0500], 0
    mov byte [es:0x0510], 0xff
    cmp byte [0x0500], 0xff
    mov [es:0x0510], ah
    mov [0x0500], al
    pop es
    ret

wait_8042:
    mov cx, 0xffff
.poll:
    in al, 0x64
    test al, 2
    loopnz .poll
    jz .ready
    stc
    ret
.ready:
    clc
    ret

bios_error:
    sti
.next:
    lodsb
    test al, al
    jz .halt
    out 0xe9, al
    mov ah, 0x0e
    mov bx, 7
    int 0x10
    jmp .next
.halt:
    hlt
    jmp .halt

align 8, db 0
boot_gdt:
    dq 0, 0x00cf9a000000ffff, 0x00cf92000000ffff
boot_gdt_pointer:
    dw $ - boot_gdt - 1
    dd boot_gdt
boot_drive: db 0
sectors_per_track: dw 18
head_count: dw 2
disk_error_msg: db "Unable to load boot payload.", 13, 10, 0
a20_error_msg: db "Unable to enable A20.", 13, 10, 0
times 510 - ($ - $$) db 0
dw 0xaa55

bits 32
section .payload start=512 vstart=PAYLOAD_ADDRESS
payload_entry:
    mov ax, DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x10000
    cld
    xor eax, eax
    mov edi, STATE_ADDRESS
    mov ecx, (state_end - state_start) / 4
    rep stosd
    mov dword [rom_size], rom_end - rom_buffer
    mov dword [steps_left], STEPS

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
    jmp print_report
.bad_rom:
    mov esi, invalid_rom_msg

; Send the replay result to VGA text and, on emulators, the debug console.
print_report:
    mov edi, 0xb8000
    mov ah, 7
.next:
    lodsb
    test al, al
    jz .halt
    out 0xe9, al
    cmp al, 10
    je .next
    stosw
    jmp .next
.halt:
    hlt
    jmp .halt

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
invalid_rom_msg: db "Unable to detect ROM mapping.", 10, 0
summary: db "ticks="
summary_tick_hi: db "00000000:"
summary_tick_lo: db "00000000 bus="
summary_bus: db "00000000 hash="
summary_hash: db "00000000 frames="
summary_frames: db "00000000 wram="
summary_wram: db "00000000 fault="
summary_fault: db "00000000 status="
summary_status: db "00000000", 10, 0

align 16, db 0
rom_buffer:
    incbin ROM_FILE
rom_end:
align 512, db 0
payload_disk_end:
; Stay below the EBDA or video RAM on a PC with standard conventional memory.
times -((payload_disk_end - payload_entry) > (0x90000 - PAYLOAD_ADDRESS)) db 0
times FLOPPY_BYTES - 512 - ($ - $$) db 0

absolute STATE_ADDRESS
state_start:
steps_left: resd 1
run_status: resd 1
CORE_STATE
resb (-($ - state_start)) & 3
state_end:
