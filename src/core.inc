; Shared headless SNES machine. Hosts provide rom_buffer and ROM_CAP.
; Calls inside the core use 32-bit registers and an internal stack convention.
%macro PUSH32 1
%if __BITS__ = 64
    lea rsp, [rsp - 8]
    mov dword [rsp], %1
%else
    push %1
%endif
%endmacro

%macro POP32 1
%if __BITS__ = 64
    mov %1, dword [rsp]
    lea rsp, [rsp + 8]
%else
    pop %1
%endif
%endmacro

%define SCAN_CLOCKS 1364
%define VBLANK_LINE 225
%define HBLANK_CLOCK 1096
%define REFRESH_CLOCK 538
%define MAP_LO 0
%define MAP_HI 1
%define MAP_EXHI 2
%ifndef TRACE_EVENTS
    %define TRACE_EVENTS 0
%endif

core_reset:
    cld
    xor eax, eax
    mov edi, machine_state_begin
    mov ecx, machine_state_end - machine_state_begin
    rep stosb
    mov eax, [rom_size]
    cmp eax, 32768
    jb .bad
    cmp eax, ROM_CAP
    ja .bad
    mov esi, rom_buffer
    mov ecx, eax
    and ecx, 0x7fff
    cmp ecx, 512
    jne .no_header
    add esi, 512
    sub eax, 512
.no_header:
    mov [cart_start], esi
    mov [cart_bytes], eax
    mov dword [trace_hash], 0x811c9dc5
    mov dword [frame_lines], 262
    mov dword [clock_deadline], REFRESH_CLOCK
    mov word [htime], 511
    mov word [vtime], 511
    mov byte [io_port], 0xff
    mov byte [rom_wait], 8
    mov edi, dma_regs
    mov ecx, 128
    mov al, 0xff
    rep stosb
    mov byte [cart_map], MAP_LO
    mov esi, 0x7fc0
    mov edi, MAP_LO
    call cart_score_header
    mov ebx, eax
    mov esi, 0xffc0
    mov edi, MAP_HI
    call cart_score_header
    cmp eax, ebx
    jle .check_extended
    mov ebx, eax
    mov byte [cart_map], MAP_HI
.check_extended:
    mov esi, 0x40ffc0
    mov edi, MAP_EXHI
    call cart_score_header
    cmp eax, ebx
    jle .use_header
    mov ebx, eax
    mov byte [cart_map], MAP_EXHI
.use_header:
    test ebx, ebx
    js .bad
    mov esi, [cart_start]
    add esi, 0x7fc0
    cmp byte [cart_map], MAP_LO
    je .header_found
    add esi, 0x8000
    cmp byte [cart_map], MAP_HI
    je .header_found
    add esi, 0x400000
.header_found:
    movzx ecx, byte [esi + 0x18]
    test ecx, ecx
    jz .region
    cmp ecx, 6
    jbe .sram_size
    mov ecx, 6
.sram_size:
    mov eax, 1024
    shl eax, cl
    mov [sram_size], eax
.region:
    mov al, [esi + 0x19]
    cmp al, 2
    jb .boot
    cmp al, 12
    ja .boot
    mov dword [frame_lines], 312
.boot:
    call ppu_reset
    call cpu_reset
    call apu_reset
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret

; ESI header offset, EDI mapper. Valid reset vector required; checksum is a hint.
cart_score_header:
    mov eax, -1
    lea edx, [esi + 64]
    cmp edx, [cart_bytes]
    ja .done
    add esi, [cart_start]
    cmp word [esi + 0x3c], 0x8000
    jb .done
    ; Accept the three supported map bytes, with either ROM speed.
    movzx edx, byte [esi + 0x15]
    and edx, 0xef
    lea ecx, [edi + 0x20]
    cmp edi, MAP_EXHI
    jne .match_map
    mov ecx, 0x25
.match_map:
    cmp edx, ecx
    jne .done
    cmp byte [esi + 0x16], 2
    ja .done
    mov eax, 4
    movzx edx, word [esi + 0x1c]
    xor dx, [esi + 0x1e]
    cmp dx, -1
    jne .done
    add eax, 3
.done:
    ret

core_step:
    cmp byte [cpu_stopped], 0
    jne .stopped
    cmp dword [cpu_fault], 0
    jne .fault
    call dma_service
    call cpu_step
    cmp dword [cpu_fault], 0
    jne .fault
    cmp byte [cpu_stopped], 0
    jne .stopped
    xor eax, eax
    ret
.stopped:
    mov eax, 1
    ret
.fault:
    mov eax, 2
    ret

; EAX is a guest tick count. Preserve guest CPU handler registers.
clock_add:
    PUSH32 ecx
    mov ecx, [hcounter]
    add ecx, eax
    cmp ecx, [clock_deadline]
    jae .slow
    test byte [nmitimen], 0x10
    jnz .slow
    mov [hcounter], ecx
    add [tick_lo], eax
    adc dword [tick_hi], 0
    POP32 ecx
    ret
.slow:
    POP32 ecx
    PUSH32 eax
    PUSH32 ebx
    PUSH32 ecx
    PUSH32 edx
    mov ebx, eax
.next:
    test ebx, ebx
    jz .done
    mov ecx, [hcounter]
    mov edx, [line_clocks]
    test edx, edx
    jnz .line_length
    mov edx, SCAN_CLOCKS
.line_length:
    cmp ecx, HBLANK_CLOCK
    jae .event
    mov edx, HBLANK_CLOCK
    cmp ecx, REFRESH_CLOCK
    jae .event
    mov edx, REFRESH_CLOCK
.event:
    sub edx, ecx
    cmp edx, ebx
    jbe .advance
    mov edx, ebx
.advance:
    sub ebx, edx
    add [hcounter], edx
    add [tick_lo], edx
    adc dword [tick_hi], 0
    call clock_irq
    mov eax, [hcounter]
    cmp eax, REFRESH_CLOCK
    je .refresh
    cmp eax, HBLANK_CLOCK
    je .hblank
    mov edx, [line_clocks]
    test edx, edx
    jnz .line_test
    mov edx, SCAN_CLOCKS
.line_test:
    cmp eax, edx
    jb .next
    mov dword [hcounter], 0
    mov dword [clock_deadline], REFRESH_CLOCK
    mov dword [line_clocks], SCAN_CLOCKS
    inc dword [vcounter]
    mov eax, [vcounter]
    cmp eax, VBLANK_LINE
    je .vblank
    cmp eax, [frame_lines]
    je .field
    cmp eax, 240
    jne .check_v_irq
    cmp dword [frame_lines], 262
    jne .check_v_irq
    cmp byte [field_odd], 0
    je .check_v_irq
    mov dword [line_clocks], 1360
    jmp .check_v_irq
.refresh:
    mov dword [clock_deadline], HBLANK_CLOCK
    ; Consume one refresh interval while continuing to advance the peripherals.
    add ebx, 40
    ; The event has fired; the next pass starts at exactly REFRESH_CLOCK.
    jmp .next
.hblank:
    mov eax, [line_clocks]
    test eax, eax
    jnz .hblank_deadline
    mov eax, SCAN_CLOCKS
.hblank_deadline:
    mov [clock_deadline], eax
    call apu_sync
    mov eax, [vcounter]
    cmp eax, VBLANK_LINE
    jae .next
    test eax, eax
    jz .queue_hdma
    PUSH32 ebx
    PUSH32 esi
    PUSH32 edi
    PUSH32 ebp
    call ppu_scanline
    POP32 ebp
    POP32 edi
    POP32 esi
    POP32 ebx
.queue_hdma:
    cmp byte [hdma_active], 0
    je .next
    mov byte [hdma_line_due], 1
    jmp .next
.vblank:
    inc dword [video_frame]
    call io_joy_begin
    mov eax, VBLANK_LINE
    mov byte [nmi_flag], 1
    test byte [nmitimen], 0x80
    jz .check_v_irq
    mov byte [nmi_pending], 1
    jmp .check_v_irq
.field:
    xor byte [field_odd], 1
    xor eax, eax
    mov [vcounter], eax
    mov [nmi_flag], al
    mov byte [hdma_init_due], 1
.check_v_irq:
    movzx edx, word [vtime]
    cmp eax, edx
    jne .next
    mov al, [nmitimen]
    and al, 0x30
    cmp al, 0x20
    jne .next
    mov byte [irq_pending], 1
    jmp .next
.done:
    POP32 edx
    POP32 ecx
    POP32 ebx
    POP32 eax
    ret

clock_irq:
    test byte [nmitimen], 0x10
    jz .done
    movzx eax, word [htime]
    shl eax, 2
    add eax, 14
    cmp eax, ecx
    jbe .done
    cmp eax, [hcounter]
    ja .done
    test byte [nmitimen], 0x20
    jz .fire
    movzx eax, word [vtime]
    cmp eax, [vcounter]
    jne .done
.fire:
    mov byte [irq_pending], 1
.done:
    ret

; CPU access time, preserving EAX. See 5A22 A-bus/B-bus wait regions.
bus_wait:
    test eax, 0x408000
    jz .low
    mov ecx, 8
    test eax, 0x800000
    jz .done
    movzx ecx, byte [rom_wait]
    ret
.low:
    lea ecx, [eax + 0x6000]
    test ecx, 0x4000
    jnz .eight
    lea ecx, [eax - 0x4000]
    test ecx, 0x7e00
    mov ecx, 6
    jnz .done
    mov ecx, 12
    ret
.eight:
    mov ecx, 8
.done:
    ret

bus_read:
    PUSH32 ebx
    PUSH32 ecx
    PUSH32 edx
    and eax, 0xffffff
    mov ebx, eax
    call bus_wait
    lea eax, [ecx - 4]
    call clock_add
    mov eax, ebx
    call bus_read_now
    mov edx, eax
    mov eax, ebx
    call trace_record
    mov eax, 4
    call clock_add
    mov eax, ebx
    and eax, 0x40fc00
    cmp eax, 0x4000
    je .latched
    mov [cpu_mdr], dl
.latched:
    mov eax, edx
    POP32 edx
    POP32 ecx
    POP32 ebx
    ret

bus_write:
    PUSH32 eax
    PUSH32 ebx
    PUSH32 ecx
    PUSH32 edx
    and eax, 0xffffff
    mov ebx, eax
    call bus_wait
    mov eax, ecx
    call clock_add
    mov [cpu_mdr], dl
    mov eax, ebx
    or eax, 0x01000000
    call trace_record
    mov eax, ebx
    call bus_write_now
    POP32 edx
    POP32 ecx
    POP32 ebx
    POP32 eax
    ret

; EAX address with flags in the high byte, EDX data (low byte). Leaves EAX/EDX.
trace_record:
%if TRACE_EVENTS
    cmp byte [trace_capture], 0
    je .hash
    call trace_append
.hash:
%endif
    PUSH32 eax
    PUSH32 ecx
    movzx ecx, dl
    xor eax, ecx
    xor eax, [tick_lo]
    rol eax, 11
    xor eax, [tick_hi]
    xor eax, [trace_hash]
    imul eax, eax, 0x01000193
    mov [trace_hash], eax
    inc dword [bus_count]
    POP32 ecx
    POP32 eax
    ret

%if TRACE_EVENTS
; A record is a little-endian 64-bit tick, 32-bit tagged address and 32-bit value.
trace_append:
    PUSH32 eax
    PUSH32 ecx
    mov ecx, [trace_used]
    cmp ecx, TRACE_EVENTS
    jae .full
    inc dword [trace_used]
    shl ecx, 4
    mov [trace_data + ecx + 8], eax
    mov eax, [tick_lo]
    mov [trace_data + ecx], eax
    mov eax, [tick_hi]
    mov [trace_data + ecx + 4], eax
    movzx eax, dl
    mov [trace_data + ecx + 12], eax
.full:
    POP32 ecx
    POP32 eax
    ret
%endif

; Untimed mapping helpers used by both CPU and DMA. EAX->EAX, ECX/EDX scratch.
bus_read_now:
    mov ecx, eax
    and ecx, 0xfe0000
    cmp ecx, 0x7e0000
    je .wram
    test eax, 0x400000
    jnz .cart
    movzx ecx, ax
    cmp ecx, 0x2000
    jb .wram_mirror
    cmp ecx, 0x6000
    jb io_read
.cart:
    call cart_map_address
    cmp ecx, 1
    je .sram
    cmp ecx, 0
    jne .open
    call cart_mirror
    add eax, [cart_start]
    movzx eax, byte [eax]
    ret
.wram:
    and eax, 0x1ffff
    movzx eax, byte [wram + eax]
    ret
.wram_mirror:
    movzx eax, byte [wram + ecx]
    ret
.sram:
    movzx eax, byte [sram + eax]
    ret
.open:
    movzx eax, byte [cpu_mdr]
    ret

bus_write_now:
    mov ecx, eax
    and ecx, 0xfe0000
    cmp ecx, 0x7e0000
    je .wram
    test eax, 0x400000
    jnz .cart
    movzx ecx, ax
    cmp ecx, 0x2000
    jb .wram_mirror
    cmp ecx, 0x6000
    jb io_write
.cart:
    PUSH32 edx
    call cart_map_address
    POP32 edx
    cmp ecx, 1
    jne .done
    mov [sram + eax], dl
.done:
    ret
.wram:
    and eax, 0x1ffff
    mov [wram + eax], dl
    ret
.wram_mirror:
    mov [wram + ecx], dl
    ret

; Return host-relative cart offset EAX and type ECX (ROM=0, SRAM=1, open=2).
cart_map_address:
    mov edx, eax
    and edx, 0x7f0000
    cmp byte [cart_map], MAP_LO
    jne .high
    cmp edx, 0x700000
    jb .low_rom
    test ax, 0x8000
    jz .low_sram
.low_rom:
    test eax, 0x408000
    jz .open
    movzx ecx, ax
    and ecx, 0x7fff
    shr edx, 1
    lea eax, [edx + ecx]
    xor ecx, ecx
    ret
.low_sram:
    movzx ecx, ax
    and edx, 0x0f0000
    shr edx, 1
    lea eax, [edx + ecx]
    jmp .sram
.high:
    test eax, 0x408000
    jnz .high_rom
    cmp edx, 0x200000
    jb .open
    cmp ax, 0x6000
    jb .open
    and edx, 0x1f0000
    shr edx, 3
    and eax, 0x1fff
    add eax, edx
    jmp .sram
.high_rom:
    mov edx, eax
    and eax, 0x3fffff
    xor ecx, ecx
    cmp byte [cart_map], MAP_EXHI
    jne .done
    test edx, 0x800000
    jnz .done
    or eax, 0x400000
.done:
    ret
.sram:
    mov edx, [sram_size]
    test edx, edx
    jz .open
    dec edx
    and eax, edx
    mov ecx, 1
    ret
.open:
    mov ecx, 2
    ret

; Mirror a non-power-of-two ROM according to its populated address lines.
cart_mirror:
    mov edx, [cart_bytes]
    cmp eax, edx
    jb .done
    PUSH32 ebx
    xor ebx, ebx
.next:
    bsr ecx, eax
    mov ecx, [bit_masks + ecx * 4]
    sub eax, ecx
    cmp edx, ecx
    jbe .again
    sub edx, ecx
    add ebx, ecx
.again:
    cmp eax, edx
    jae .next
    add eax, ebx
    POP32 ebx
.done:
    ret

bit_masks:
%assign bit_index 0
%rep 24
    dd 1 << bit_index
%assign bit_index bit_index+1
%endrep

%include "apu.inc"
%include "io.inc"
%include "dma.inc"
%include "cpu.inc"
%include "ppu.inc"

%macro CORE_STATE 0
    alignb 4
    rom_size:        resd 1
machine_state_begin:
    cart_start:      resd 1
    cart_bytes:      resd 1
    sram_size:       resd 1
    tick_lo:         resd 1
    tick_hi:         resd 1
    trace_hash:      resd 1
    bus_count:       resd 1
%if TRACE_EVENTS
    trace_used:      resd 1
%endif
    hcounter:        resd 1
    vcounter:        resd 1
    line_clocks:     resd 1
    clock_deadline:  resd 1
    frame_lines:     resd 1
    cart_map:        resb 1
    rom_wait:        resb 1
    cpu_mdr:         resb 1
    field_odd:       resb 1
    irq_pending:     resb 1
    nmi_pending:     resb 1
    nmitimen:        resb 1
    nmi_flag:        resb 1
    htime:           resw 1
    vtime:           resw 1
    APU_STATE
    IO_STATE
    DMA_STATE
    CPU_STATE
    PPU_STATE
    alignb 16
    wram:            resb 128 * 1024
    sram:            resb 64 * 1024
machine_state_end:
%if TRACE_EVENTS
    trace_capture:   resb 1
    alignb 16
    trace_data:      resb TRACE_EVENTS * 16
%endif
%endmacro
