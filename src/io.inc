; EAX is the full A-bus address and ECX the decoded register. ECX/EDX scratch.
io_read:
    cmp ecx, 0x2140
    jb .ppu
    cmp ecx, 0x217f
    jbe apu_main_read
    cmp ecx, 0x2180
    je .wram
    cmp ecx, 0x4016
    je .pad_one
    cmp ecx, 0x4017
    je .pad_two
    cmp ecx, 0x4210
    je .nmi
    cmp ecx, 0x4211
    je .irq
    cmp ecx, 0x4212
    je .blank
    cmp ecx, 0x4213
    je .port
    cmp ecx, 0x4214
    jb .open
    cmp ecx, 0x4217
    jbe .math
    cmp ecx, 0x421f
    jbe .auto_joy
    sub ecx, 0x4300
    cmp ecx, 128
    jae .open
    ; The duplicate register at xF aliases xB; unused registers return MDR.
    mov eax, ecx
    and eax, 15
    cmp eax, 15
    je .dma_alias
    cmp eax, 11
    ja .open
    jmp .dma
.dma_alias:
    sub ecx, 4
.dma:
    movzx eax, byte [dma_regs + ecx]
    ret
.ppu:
    cmp ecx, 0x2100
    jb .open
    PUSH32 ebx
    PUSH32 esi
    PUSH32 edi
    PUSH32 ebp
    mov eax, ecx
    call ppu_read
    POP32 ebp
    POP32 edi
    POP32 esi
    POP32 ebx
    test eax, eax
    js .open
    ret
.open:
    movzx eax, byte [cpu_mdr]
    ret
.wram:
    mov ecx, [wram_pointer]
    lea eax, [ecx + 1]
    and eax, 0x1ffff
    mov [wram_pointer], eax
    movzx eax, byte [wram + ecx]
    ret
.pad_one:
    xor ecx, ecx
    jmp .serial
.pad_two:
    mov ecx, 2
.serial:
    movzx eax, byte [cpu_mdr]
    and eax, 0xfc
    cmp ecx, 0
    je .serial_bits
    or eax, 0x1c
.serial_bits:
    cmp byte [joy_strobe], 0
    jne .strobe
    movzx edx, word [joy_shift + ecx]
    shl word [joy_shift + ecx], 1
    or word [joy_shift + ecx], 1
    jmp .serial_end
.strobe:
    movzx edx, word [joy_input + ecx]
.serial_end:
    shr edx, 15
    or eax, edx
    ret
.nmi:
    movzx eax, byte [cpu_mdr]
    and eax, 0x70
    or eax, 0x02
    cmp byte [nmi_flag], 0
    je .return
    or eax, 0x80
    mov byte [nmi_flag], 0
.return:
    ret
.irq:
    movzx eax, byte [cpu_mdr]
    and eax, 0x7f
    cmp byte [irq_pending], 0
    je .return
    or eax, 0x80
    mov byte [irq_pending], 0
    ret
.blank:
    call io_joy_update
    movzx eax, byte [cpu_mdr]
    and eax, 0x3e
    cmp dword [hcounter], 2
    jbe .horizontal_blank
    cmp dword [hcounter], HBLANK_CLOCK
    jb .vertical
.horizontal_blank:
    or eax, 0x40
.vertical:
    cmp dword [vcounter], VBLANK_LINE
    jb .busy
    or eax, 0x80
.busy:
    or al, [joy_poll_active]
    ret
.port:
    movzx eax, byte [io_port]
    ret
.math:
    call io_math_update
    movzx eax, byte [math_result + ecx - 0x4214]
    ret
.auto_joy:
    call io_joy_update
    movzx eax, byte [joy_result + ecx - 0x4218]
    ret

io_write:
    cmp ecx, 0x2140
    jb .ppu
    cmp ecx, 0x217f
    jbe apu_main_write
    cmp ecx, 0x2180
    je .wram
    cmp ecx, 0x2181
    je .wram_low
    cmp ecx, 0x2182
    je .wram_mid
    cmp ecx, 0x2183
    je .wram_high
    cmp ecx, 0x4016
    je .joy
    cmp ecx, 0x4200
    je .nmitimen
    cmp ecx, 0x4201
    je .port
    cmp ecx, 0x4202
    je .multiplicand
    cmp ecx, 0x4203
    je .multiply
    cmp ecx, 0x4204
    je .dividend_low
    cmp ecx, 0x4205
    je .dividend_high
    cmp ecx, 0x4206
    je .divide
    cmp ecx, 0x4207
    je .htime_low
    cmp ecx, 0x4208
    je .htime_high
    cmp ecx, 0x4209
    je .vtime_low
    cmp ecx, 0x420a
    je .vtime_high
    cmp ecx, 0x420b
    je .dma_request
    cmp ecx, 0x420c
    je .hdma_enable
    cmp ecx, 0x420d
    je .fastrom
    sub ecx, 0x4300
    cmp ecx, 128
    jae .done
    mov eax, ecx
    and eax, 15
    cmp eax, 15
    je .dma_alias
    cmp eax, 11
    ja .done
    jmp .dma
.dma_alias:
    sub ecx, 4
.dma:
    mov [dma_regs + ecx], dl
.done:
    ret
.ppu:
    cmp ecx, 0x2100
    jb .done
    PUSH32 ebx
    PUSH32 esi
    PUSH32 edi
    PUSH32 ebp
    mov eax, ecx
    call ppu_write
    POP32 ebp
    POP32 edi
    POP32 esi
    POP32 ebx
    ret
.wram:
    mov ecx, [wram_pointer]
    mov [wram + ecx], dl
    inc ecx
    and ecx, 0x1ffff
    mov [wram_pointer], ecx
    ret
.wram_low:
    mov [wram_pointer], dl
    ret
.wram_mid:
    mov [wram_pointer + 1], dl
    ret
.wram_high:
    and dl, 1
    mov [wram_pointer + 2], dl
    ret
.joy:
    and dl, 1
    cmp dl, [joy_strobe]
    je .done
    mov [joy_strobe], dl
    mov eax, [joy_input]
    mov [joy_shift], eax
    ret
.nmitimen:
    mov al, [nmitimen]
    mov [nmitimen], dl
    test dl, 0x30
    jnz .nmi_enable
    mov byte [irq_pending], 0
.nmi_enable:
    test al, 0x80
    jnz .done
    test dl, 0x80
    jz .done
    cmp byte [nmi_flag], 0
    je .done
    mov byte [nmi_pending], 1
    ret
.port:
    mov al, [io_port]
    mov [io_port], dl
    test al, 0x80
    jz .done
    test dl, 0x80
    jnz .done
    jmp ppu_latch_counters
.multiplicand:
    mov [math_a], dl
    ret
.multiply:
    call io_math_update
    cmp byte [math_pending], 0
    jne .done
    movzx eax, byte [math_a]
    movzx edx, dl
    imul eax, edx
    mov [math_next + 2], ax
    mov ah, dl
    mov al, [math_a]
    mov [math_next], ax
    mov eax, [tick_lo]
    add eax, 48
    jmp .math_schedule
.dividend_low:
    mov [math_dividend], dl
    ret
.dividend_high:
    mov [math_dividend + 1], dl
    ret
.divide:
    call io_math_update
    cmp byte [math_pending], 0
    jne .done
    movzx eax, word [math_dividend]
    movzx ecx, dl
    test ecx, ecx
    jz .divide_zero
    xor edx, edx
    div ecx
    jmp .divide_store
.divide_zero:
    mov edx, eax
    mov eax, 0xffff
.divide_store:
    mov [math_next], ax
    mov [math_next + 2], dx
    mov eax, [tick_lo]
    add eax, 96
.math_schedule:
    mov [math_due], eax
    mov byte [math_pending], 1
    ret
.htime_low:
    mov [htime], dl
    ret
.htime_high:
    and dl, 1
    mov [htime + 1], dl
    ret
.vtime_low:
    mov [vtime], dl
    ret
.vtime_high:
    and dl, 1
    mov [vtime + 1], dl
    ret
.dma_request:
    mov [dma_pending], dl
    ret
.hdma_enable:
    mov [hdma_enable], dl
    and [hdma_active], dl
    ret
.fastrom:
    and dl, 1
    shl dl, 1
    mov al, 8
    sub al, dl
    mov [rom_wait], al
    ret

io_math_update:
    PUSH32 eax
    cmp byte [math_pending], 0
    je .done
    mov eax, [tick_lo]
    sub eax, [math_due]
    js .done
    mov eax, [math_next]
    mov [math_result], eax
    mov byte [math_pending], 0
.done:
    POP32 eax
    ret

io_joy_begin:
    test byte [nmitimen], 1
    jz .done
    mov eax, [tick_lo]
    add eax, 4224
    mov [joy_due], eax
    mov byte [joy_poll_active], 1
.done:
    ret

io_joy_update:
    PUSH32 eax
    cmp byte [joy_poll_active], 0
    je .done
    mov eax, [tick_lo]
    sub eax, [joy_due]
    js .done
    mov eax, [joy_input]
    mov [joy_result], eax
    mov byte [joy_poll_active], 0
.done:
    POP32 eax
    ret

%macro IO_STATE 0
    alignb 4
    wram_pointer:    resd 1
    math_due:        resd 1
    math_result:     resd 1
    math_next:       resd 1
    joy_due:         resd 1
    joy_input:       resd 1
    joy_shift:       resd 1
    joy_result:      resd 2
    math_dividend:   resw 1
    io_port:         resb 1
    math_a:          resb 1
    math_pending:    resb 1
    joy_strobe:      resb 1
    joy_poll_active: resb 1
%endmacro
