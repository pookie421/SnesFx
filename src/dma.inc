; DMA/HDMA bookkeeping. General DMA is serviced between CPU instructions.
dma_service:
    call hdma_service
    movzx ebp, byte [dma_pending]
    test ebp, ebp
    jz .done
    mov [dma_pending], byte 0
    mov eax, [tick_lo]
    and eax, 7
    neg eax
    add eax, 8
    call clock_add
    xor edi, edi
.channel:
    test ebp, 1
    jz .next
    mov eax, 8
    call clock_add
    xor esi, esi
.byte:
    movzx eax, byte [dma_regs + edi + 4]
    shl eax, 16
    mov ax, [dma_regs + edi + 2]
    movzx edx, byte [dma_regs + edi]
    call dma_target
    call dma_transfer
    mov al, [dma_regs + edi]
    test al, 8
    jnz .next_byte
    test al, 16
    jnz .decrement
    inc word [dma_regs + edi + 2]
    jmp .next_byte
.decrement:
    dec word [dma_regs + edi + 2]
.next_byte:
    inc esi
    call hdma_service
    dec word [dma_regs + edi + 5]
    jnz .byte
.next:
    add edi, 16
    shr ebp, 1
    jnz .channel
.done:
    ret

; EDI is the channel offset. ESI is the transfer byte index. EDX is DMAP.
; Outputs ECX = B-bus address. Preserves EAX (A-bus address) and EDX.
dma_target:
    PUSH32 eax
    mov ecx, edx
    and ecx, 7
    shl ecx, 2
    mov eax, esi
    and eax, 3
    add ecx, eax
    movzx eax, byte [dma_pattern + ecx]
    add al, [dma_regs + edi + 1]
    movzx ecx, al
    or ecx, 0x2100
    POP32 eax
    ret

dma_pattern:
    db 0,0,0,0, 0,1,0,1, 0,0,0,0, 0,0,1,1
    db 0,1,2,3, 0,1,0,1, 0,0,0,0, 0,0,1,1

; Each byte uses a read at +4 ticks and a write at +8. EAX A, ECX B, EDX DMAP.
dma_transfer:
    PUSH32 ebx
    PUSH32 ebp
    PUSH32 esi
    mov ebx, eax
    mov ebp, ecx
    xor esi, esi
    call dma_valid_a
    jnc .check_wram
    or esi, 1
.check_wram:
    cmp ecx, 0x2180
    jne .direction
    mov eax, ebx
    and eax, 0xfe0000
    cmp eax, 0x7e0000
    je .wram
    mov eax, ebx
    and eax, 0x40e000
    jnz .direction
.wram:
    or esi, 2
.direction:
    test dl, 0x80
    jz .transfer
    xchg ebx, ebp
    ; Swap source and destination validity flags.
    cmp esi, 1
    jb .transfer
    cmp esi, 2
    ja .transfer
    xor esi, 3
.transfer:
    mov eax, 4
    call clock_add
    xor eax, eax
    test esi, 1
    jnz .read
    mov eax, ebx
    call bus_read_now
.read:
    mov edx, eax
    mov [cpu_mdr], dl
    mov eax, ebx
    or eax, 0x02000000
    call trace_record
    mov eax, 4
    call clock_add
    test esi, 2
    jnz .done
    mov eax, ebp
    or eax, 0x03000000
    call trace_record
    mov eax, ebp
    call bus_write_now
.done:
    POP32 esi
    POP32 ebp
    POP32 ebx
    ret

; Carry set for addresses the DMA A bus cannot access. Preserve all GPRs.
dma_valid_a:
    test eax, 0x400000
    jnz .valid
    cmp ax, 0x2100
    jb .valid
    cmp ax, 0x2200
    jb .invalid
    cmp ax, 0x4000
    jb .valid
    cmp ax, 0x4220
    jb .invalid
    cmp ax, 0x4300
    jb .valid
    cmp ax, 0x4380
    jb .invalid
.valid:
    clc
    ret
.invalid:
    stc
    ret

; Run the pending HDMA work at a CPU instruction or general DMA byte boundary.
hdma_service:
    cmp byte [hdma_init_due], 0
    jne .work
    cmp byte [hdma_line_due], 0
    je .done
.work:
    PUSH32 ebx
    PUSH32 ebp
    PUSH32 edi
    PUSH32 esi
    cmp byte [hdma_init_due], 0
    je .line
    mov byte [hdma_init_due], 0
    mov al, [hdma_enable]
    mov [hdma_active], al
    mov [hdma_repeat], al
    movzx ebp, al
    test ebp, ebp
    jz .line
    mov eax, 8
    call clock_add
    mov ebx, 1
    xor edi, edi
.setup_channel:
    test ebp, 1
    jz .next_setup
    mov ax, [dma_regs + edi + 2]
    mov [dma_regs + edi + 8], ax
    call hdma_reload
.next_setup:
    shl ebx, 1
    add edi, 16
    shr ebp, 1
    jnz .setup_channel
.line:
    cmp byte [hdma_line_due], 0
    je .restore
    mov byte [hdma_line_due], 0
    movzx ebp, byte [hdma_active]
    test ebp, ebp
    jz .restore
    mov eax, 8
    call clock_add
    mov ebx, 1
    xor edi, edi
.line_channel:
    test ebp, 1
    jz .next_line
    test [hdma_repeat], bl
    jz .line_count
    xor esi, esi
.hdma_byte:
    movzx edx, byte [dma_regs + edi]
    test dl, 0x40
    jnz .indirect
    movzx eax, byte [dma_regs + edi + 4]
    shl eax, 16
    mov ax, [dma_regs + edi + 8]
    inc word [dma_regs + edi + 8]
    jmp .hdma_transfer
.indirect:
    movzx eax, byte [dma_regs + edi + 7]
    shl eax, 16
    mov ax, [dma_regs + edi + 5]
    inc word [dma_regs + edi + 5]
.hdma_transfer:
    call dma_target
    call dma_transfer
    inc esi
    movzx ecx, byte [dma_regs + edi]
    and ecx, 7
    movzx ecx, byte [hdma_lengths + ecx]
    cmp esi, ecx
    jb .hdma_byte
.line_count:
    dec byte [dma_regs + edi + 10]
    mov al, [dma_regs + edi + 10]
    test al, 0x7f
    jz .reload
    test al, 0x80
    jnz .next_line
    mov al, bl
    not al
    and [hdma_repeat], al
    jmp .next_line
.reload:
    call hdma_reload
.next_line:
    shl ebx, 1
    add edi, 16
    shr ebp, 1
    jnz .line_channel
.restore:
    POP32 esi
    POP32 edi
    POP32 ebp
    POP32 ebx
.done:
    ret

hdma_lengths:
    db 1,2,2,4,4,4,2,4

; Read a line descriptor and, when needed, its two-byte indirect address.
hdma_reload:
    call hdma_table_byte
    mov [dma_regs + edi + 10], al
    test al, al
    jz .end
    or [hdma_repeat], bl
    test byte [dma_regs + edi], 0x40
    jz .done
    call hdma_table_byte
    mov [dma_regs + edi + 5], al
    call hdma_table_byte
    mov [dma_regs + edi + 6], al
.done:
    ret
.end:
    mov al, bl
    not al
    and [hdma_active], al
    and [hdma_repeat], al
    ret

hdma_table_byte:
    mov eax, 4
    call clock_add
    movzx eax, byte [dma_regs + edi + 4]
    shl eax, 16
    mov ax, [dma_regs + edi + 8]
    inc word [dma_regs + edi + 8]
    PUSH32 eax
    call dma_valid_a
    jc .invalid
    call bus_read_now
    jmp .read
.invalid:
    xor eax, eax
.read:
    mov edx, eax
    mov [cpu_mdr], dl
    POP32 eax
    or eax, 0x02000000
    call trace_record
    mov eax, 4
    call clock_add
    mov eax, edx
    ret

%macro DMA_STATE 0
    dma_pending:     resb 1
    hdma_enable:     resb 1
    hdma_active:     resb 1
    hdma_repeat:     resb 1
    hdma_line_due:   resb 1
    hdma_init_due:   resb 1
    dma_regs:        resb 128
%endmacro
