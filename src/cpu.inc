; Ricoh 5A22 instruction core. Code is single-instance and uses the timed
; system bus for each byte. EAX is the bus address and EDX is write data.

%define CPU_C  0x01
%define CPU_Z  0x02
%define CPU_I  0x04
%define CPU_DF 0x08
%define CPU_XF 0x10
%define CPU_M  0x20
%define CPU_V  0x40
%define CPU_N  0x80

%macro CPU_STATE 0
cpu_a:          resw 1
cpu_x:          resw 1
cpu_y:          resw 1
cpu_s:          resw 1
cpu_d:          resw 1
cpu_pc:         resw 1
cpu_pbr:        resb 1
cpu_dbr:        resb 1
cpu_p:          resb 1
cpu_e:          resb 1
cpu_waiting:    resb 1
cpu_stopped:    resb 1
cpu_irq_poll:   resb 1
cpu_fault:      resd 1
%endmacro

cpu_reset:
    mov byte [cpu_e], 1
    mov byte [cpu_p], CPU_I | CPU_M | CPU_XF
    mov word [cpu_s], 0x01ff
    mov esi, 0xffff
    mov eax, 0xfffc
    call cpu_read16
    mov [cpu_pc], ax
    ret

cpu_step:
    cmp byte [cpu_stopped], 0
    jne cpu_idle
    cmp byte [nmi_pending], 0
    jne .nmi
    movzx eax, byte [cpu_irq_poll]
    mov byte [cpu_irq_poll], 0
    cmp eax, 2
    je .irq
    cmp eax, 1
    je .fetch
    cmp byte [irq_pending], 0
    je .check_wait
    call .wake
    test byte [cpu_p], CPU_I
    jz .irq
.check_wait:
    cmp byte [cpu_waiting], 0
    jne cpu_idle
.fetch:
    call cpu_fetch8
%if __BITS__ = 64
    jmp [cpu_dispatch + eax * 8]
%else
    jmp [cpu_dispatch + eax * 4]
%endif
.nmi:
    mov byte [nmi_pending], 0
    mov ebx, 0xffea
    cmp byte [cpu_e], 0
    je .interrupt
    mov ebx, 0xfffa
    jmp .interrupt
.irq:
    mov ebx, 0xffee
    cmp byte [cpu_e], 0
    je .interrupt
    mov ebx, 0xfffe
.interrupt:
    mov byte [cpu_irq_poll], 0
    call .wake
    call cpu_dummy_pc
    call cpu_idle
    xor ebp, ebp
    jmp cpu_push_interrupt
.wake:
    cmp byte [cpu_waiting], 0
    je .awake
    mov byte [cpu_waiting], 0
    jmp cpu_idle
.awake:
    ret

; Instructions that commit P at their last cycle poll with the old I bit.
cpu_poll_irq_before_p:
    mov byte [cpu_irq_poll], 1
    test byte [cpu_p], CPU_I
    jnz .done
    cmp byte [irq_pending], 0
    je .done
    inc byte [cpu_irq_poll]
.done:
    ret

cpu_idle:
    PUSH32 eax
    mov eax, 6
    call clock_add
    POP32 eax
    ret

; A final implied cycle becomes a bus read when an interrupt is already latched.
cpu_idle_irq:
    cmp byte [nmi_pending], 0
    jne cpu_dummy_pc
    cmp byte [irq_pending], 0
    je cpu_idle
    test byte [cpu_p], CPU_I
    jnz cpu_idle
cpu_dummy_pc:
    movzx eax, byte [cpu_pbr]
    shl eax, 16
    mov ax, [cpu_pc]
    jmp bus_read

cpu_fetch8:
    movzx eax, byte [cpu_pbr]
    shl eax, 16
    mov ax, [cpu_pc]
    inc word [cpu_pc]
    jmp bus_read

cpu_fetch16:
    call cpu_fetch8
    mov edx, eax
    call cpu_fetch8
    shl eax, 8
    or eax, edx
    ret

cpu_fetch24:
    call cpu_fetch16
    mov edx, eax
    call cpu_fetch8
    shl eax, 16
    or eax, edx
    ret

; ESI is the wrap mask. Results preserve the low byte in ECX between reads.
cpu_next_address:
    mov edx, esi
    not edx
    and edx, eax
    inc eax
    and eax, esi
    or eax, edx
    ret

cpu_read16:
    mov edi, eax
    call bus_read
    mov ecx, eax
    mov eax, edi
    call cpu_next_address
    call bus_read
    shl eax, 8
    or eax, ecx
    ret

cpu_read24:
    call cpu_read16
    mov ecx, eax
    mov eax, edi
    call cpu_next_address
    call cpu_next_address
    call bus_read
    shl eax, 16
    or eax, ecx
    ret

cpu_push8:
    mov edx, eax
    movzx eax, word [cpu_s]
    call bus_write
    cmp byte [cpu_e], 0
    jne .emulation
    dec word [cpu_s]
    ret
.emulation:
    dec byte [cpu_s]
    ret

cpu_push8_native:
    mov edx, eax
    movzx eax, word [cpu_s]
    call bus_write
    dec word [cpu_s]
    ret

cpu_pull8:
    cmp byte [cpu_e], 0
    jne .emulation
    inc word [cpu_s]
    jmp .read
.emulation:
    inc byte [cpu_s]
.read:
    movzx eax, word [cpu_s]
    jmp bus_read

cpu_pull8_native:
    inc word [cpu_s]
    movzx eax, word [cpu_s]
    jmp bus_read

%macro CPU_STACK_PAIR 4
%1:
    PUSH32 eax
    shr eax, 8
    call %3
    POP32 eax
    jmp %3

%2:
    call %4
    mov ecx, eax
    call %4
    shl eax, 8
    or eax, ecx
    ret
%endmacro

CPU_STACK_PAIR cpu_push16, cpu_pull16, cpu_push8, cpu_pull8
CPU_STACK_PAIR cpu_push16_native, cpu_pull16_native, cpu_push8_native, cpu_pull8_native

cpu_force_stack_page:
    cmp byte [cpu_e], 0
    je .done
    mov byte [cpu_s + 1], 1
.done:
    ret

cpu_normalize_p:
    cmp byte [cpu_e], 0
    je .native
    or byte [cpu_p], CPU_XF | CPU_M
    mov byte [cpu_s + 1], 1
.native:
    test byte [cpu_p], CPU_XF
    jz .done
    mov byte [cpu_x + 1], 0
    mov byte [cpu_y + 1], 0
.done:
    ret

cpu_push_interrupt:
    cmp byte [cpu_e], 0
    jne .emulation
    movzx eax, byte [cpu_pbr]
    call cpu_push8
.emulation:
    movzx eax, word [cpu_pc]
    call cpu_push16
    movzx eax, byte [cpu_p]
    cmp byte [cpu_e], 0
    je .status
    and eax, ~CPU_XF
    or eax, ebp
.status:
    call cpu_push8
    or byte [cpu_p], CPU_I
    and byte [cpu_p], ~CPU_DF
    mov byte [cpu_pbr], 0
    mov eax, ebx
    mov esi, 0xffff
    call cpu_read16
    mov [cpu_pc], ax
    ret

; Effective address helpers take EBP=0 for reads and EBP=1 for writes or RMW.
; EAX returns the effective address, ESI returns the word-access wrap mask.
cpu_addr_dp_start:
    call cpu_fetch8
    cmp byte [cpu_d], 0
    je .done
    call cpu_idle
.done:
    ret

cpu_addr_dp:
    call cpu_addr_dp_start
    jmp cpu_addr_direct
cpu_addr_dpx:
    call cpu_addr_dp_start
    call cpu_idle
    movzx edx, word [cpu_x]
    add eax, edx
    jmp cpu_addr_direct
cpu_addr_dpy:
    call cpu_addr_dp_start
    call cpu_idle
    movzx edx, word [cpu_y]
    add eax, edx
cpu_addr_direct:
    cmp byte [cpu_e], 0
    je cpu_addr_direct_native
    cmp byte [cpu_d], 0
    jne cpu_addr_direct_native
    and eax, 0xff
    or ah, [cpu_d + 1]
    mov esi, 0xff
    ret

cpu_addr_direct_native:
    add ax, [cpu_d]
    and eax, 0xffff
    mov esi, 0xffff
    ret

cpu_addr_dpi:
    call cpu_addr_dp
    call cpu_read16
    jmp cpu_addr_data_bank

cpu_addr_dpix:
    call cpu_addr_dpx
    cmp byte [cpu_e], 0
    je .read_pointer
    ; (dp,X) also wraps the pointer high-byte fetch when DL is nonzero.
    mov esi, 0xff
.read_pointer:
    call cpu_read16
    jmp cpu_addr_data_bank

cpu_addr_dpiy:
    call cpu_addr_dp
    call cpu_read16
    jmp cpu_addr_index_y

cpu_addr_dpl:
    call cpu_addr_dp_start
    call cpu_addr_direct_native
    call cpu_read24
    mov esi, 0xffffff
    ret

cpu_addr_dply:
    call cpu_addr_dpl
    movzx edx, word [cpu_y]
    add eax, edx
    and eax, 0xffffff
    ret

cpu_addr_sr:
    call cpu_fetch8
    call cpu_idle
    add ax, [cpu_s]
    mov esi, 0xffff
    ret

cpu_addr_sriy:
    call cpu_addr_sr
    call cpu_read16
    call cpu_idle
    movzx edx, word [cpu_y]
    add eax, edx
    jmp cpu_addr_data_bank

cpu_addr_abs:
    call cpu_fetch16
    jmp cpu_addr_data_bank

cpu_addr_absx:
    call cpu_fetch16
    movzx edx, word [cpu_x]
    jmp cpu_addr_index
cpu_addr_absy:
    call cpu_fetch16
cpu_addr_index_y:
    movzx edx, word [cpu_y]
cpu_addr_index:
    mov ecx, eax
    add eax, edx
    test ebp, ebp
    jnz .extra
    test byte [cpu_p], CPU_XF
    jz .extra
    xor ecx, eax
    test ecx, 0xff00
    jz cpu_addr_data_bank
.extra:
    call cpu_idle
cpu_addr_data_bank:
    movzx edx, byte [cpu_dbr]
    shl edx, 16
    add eax, edx
    and eax, 0xffffff
    mov esi, 0xffffff
    ret

cpu_addr_long:
    call cpu_fetch24
    mov esi, 0xffffff
    ret
cpu_addr_longx:
    call cpu_addr_long
    movzx edx, word [cpu_x]
    add eax, edx
    and eax, 0xffffff
    ret

; EBX encodes operand width as a byte count.
cpu_width_a:
    mov ebx, 1
    test byte [cpu_p], CPU_M
    jnz .done
    inc ebx
.done:
    ret
cpu_width_x:
    mov ebx, 1
    test byte [cpu_p], CPU_XF
    jnz .done
    inc ebx
.done:
    ret

cpu_read_width:
    cmp ebx, 1
    je bus_read
    jmp cpu_read16

cpu_read_imm:
    cmp ebx, 1
    je cpu_fetch8
    jmp cpu_fetch16

cpu_write_width:
    call bus_write
    cmp ebx, 1
    je .done
    mov ecx, edx
    call cpu_next_address
    mov edx, ecx
    shr edx, 8
    jmp bus_write
.done:
    ret

cpu_write_rmw:
    cmp ebx, 1
    je bus_write
    PUSH32 eax
    mov ecx, edx
    call cpu_next_address
    mov edx, ecx
    shr edx, 8
    call bus_write
    POP32 eax
    mov edx, ecx
    jmp bus_write

cpu_set_nz:
    and byte [cpu_p], ~(CPU_N | CPU_Z)
    cmp ebx, 1
    je .byte
    test ax, ax
    jmp .flags
.byte:
    test al, al
.flags:
    js .negative
    jnz .done
    or byte [cpu_p], CPU_Z
.done:
    ret
.negative:
    or byte [cpu_p], CPU_N
    ret

%macro CPU_STORE_REG 1
    mov [%1], al
    cmp ebx, 1
    je %%done
    mov [%1 + 1], ah
%%done:
%endmacro

%macro CPU_LOAD_REG 1
    movzx eax, word [%1]
    cmp ebx, 1
    jne %%done
    and eax, 0xff
%%done:
%endmacro

%macro CPU_LOGIC 2
cpu_alu_%1:
    %2 ax, [cpu_a]
    CPU_STORE_REG cpu_a
    jmp cpu_set_nz
%endmacro

CPU_LOGIC ora, or
CPU_LOGIC and, and
CPU_LOGIC eor, xor

%macro CPU_LOAD 2
cpu_alu_%1:
    CPU_STORE_REG %2
    jmp cpu_set_nz
%endmacro

CPU_LOAD lda, cpu_a
CPU_LOAD ldx, cpu_x
CPU_LOAD ldy, cpu_y

%macro CPU_COMPARE 2
cpu_alu_%1:
    mov edx, eax
    CPU_LOAD_REG %2
    and byte [cpu_p], ~CPU_C
    sub eax, edx
    jc %%flags
    or byte [cpu_p], CPU_C
%%flags:
    jmp cpu_set_nz
%endmacro

CPU_COMPARE cmp, cpu_a
CPU_COMPARE cpx, cpu_x
CPU_COMPARE cpy, cpu_y

cpu_alu_bit:
    and byte [cpu_p], ~(CPU_N | CPU_V)
    mov edx, eax
    cmp ebx, 1
    je .high
    shr edx, 8
.high:
    and edx, CPU_N | CPU_V
    or [cpu_p], dl
cpu_alu_biti:
    and byte [cpu_p], ~CPU_Z
    mov edx, eax
    and dx, [cpu_a]
    cmp ebx, 1
    jne .word
    test dl, dl
    jmp .flags
.word:
    test dx, dx
.flags:
    jnz .done
    or byte [cpu_p], CPU_Z
.done:
    ret

cpu_alu_tsb:
    call cpu_alu_biti
    or ax, [cpu_a]
    ret

cpu_alu_trb:
    call cpu_alu_biti
    movzx edx, word [cpu_a]
    not edx
    and eax, edx
    ret

; The corrected carry of each nonfinal nibble enters the next nibble.
; Overflow is derived before the high-nibble adjustment.
%macro CPU_DECIMAL_NIBBLE 1
    mov edx, edi
    and edx, 0xf << %1
    add eax, edx
    mov edx, esi
    and edx, 0xf << %1
    add eax, edx
%endmacro

%macro CPU_DECIMAL_ADJUST 1
    test ebp, ebp
    jnz %%subtract
    cmp eax, ((0xa << %1) - 1)
    jle %%done
    add eax, 0x6 << %1
    jmp %%done
%%subtract:
    cmp eax, ((0x10 << %1) - 1)
    jg %%done
    sub eax, 0x6 << %1
%%done:
%endmacro

%macro CPU_DECIMAL_CARRY 1
    xor edx, edx
    cmp eax, ((0x10 << %1) - 1)
    setg dl
    shl edx, %1 + 4
    and eax, ((0x10 << %1) - 1)
    or eax, edx
%endmacro

cpu_alu_adc:
    xor ebp, ebp
    jmp cpu_alu_add
cpu_alu_sbc:
    mov ebp, 1
    xor eax, 0xffff
    cmp ebx, 1
    jne cpu_alu_add
    and eax, 0xff
cpu_alu_add:
    mov esi, eax
    CPU_LOAD_REG cpu_a
    mov edi, eax
    movzx eax, byte [cpu_p]
    and eax, CPU_C
    test byte [cpu_p], CPU_DF
    jnz .decimal
    add eax, esi
    add eax, edi
    jmp .overflow
.decimal:
    CPU_DECIMAL_NIBBLE 0
    CPU_DECIMAL_ADJUST 0
    CPU_DECIMAL_CARRY 0
    CPU_DECIMAL_NIBBLE 4
    cmp ebx, 1
    je .overflow
    CPU_DECIMAL_ADJUST 4
    CPU_DECIMAL_CARRY 4
    CPU_DECIMAL_NIBBLE 8
    CPU_DECIMAL_ADJUST 8
    CPU_DECIMAL_CARRY 8
    CPU_DECIMAL_NIBBLE 12
.overflow:
    and byte [cpu_p], ~(CPU_C | CPU_V)
    mov edx, edi
    xor edx, esi
    not edx
    mov ecx, edi
    xor ecx, eax
    and edx, ecx
    cmp ebx, 1
    je .byte
    test edx, 0x8000
    jz .correct_word
    or byte [cpu_p], CPU_V
.correct_word:
    test byte [cpu_p], CPU_DF
    jz .carry_word
    CPU_DECIMAL_ADJUST 12
.carry_word:
    cmp eax, 0xffff
    jmp .carry
.byte:
    test edx, 0x80
    jz .correct_byte
    or byte [cpu_p], CPU_V
.correct_byte:
    test byte [cpu_p], CPU_DF
    jz .carry_byte
    CPU_DECIMAL_ADJUST 4
.carry_byte:
    cmp eax, 0xff
.carry:
    jle .store
    or byte [cpu_p], CPU_C
.store:
    CPU_STORE_REG cpu_a
    jmp cpu_set_nz

%macro CPU_SHIFT 2
cpu_alu_%1:
    movzx edx, byte [cpu_p]
    cmp ebx, 1
    je %%byte
    bt edx, 0
    %2 ax, 1
    jmp %%carry
%%byte:
    bt edx, 0
    %2 al, 1
%%carry:
    setc dl
    and byte [cpu_p], ~CPU_C
    or byte [cpu_p], dl
    jmp cpu_set_nz
%endmacro

CPU_SHIFT asl, shl
CPU_SHIFT lsr, shr
CPU_SHIFT rol, rcl
CPU_SHIFT ror, rcr

cpu_alu_inc:
    inc eax
    jmp cpu_set_nz

cpu_alu_dec:
    dec eax
    jmp cpu_set_nz

%macro CPU_READ_OP 4
cpu_op_%1:
    call cpu_width_%4
%ifidni %3, imm
    call cpu_read_imm
%else
    xor ebp, ebp
    call cpu_addr_%3
    call cpu_read_width
%endif
    jmp cpu_alu_%2
%endmacro

%macro CPU_WRITE_OP 4
cpu_op_%1:
    call cpu_width_%4
    mov ebp, 1
    call cpu_addr_%3
%ifidni %2, zero
    xor edx, edx
%else
    movzx edx, word [%2]
%endif
    jmp cpu_write_width
%endmacro

%macro CPU_RMW_OP 3
cpu_op_%1:
    call cpu_width_a
    mov ebp, 1
    call cpu_addr_%3
    PUSH32 eax
    call cpu_read_width
    call cpu_idle
    ; Arithmetic can reuse ESI, so keep the wrap mask beside the address.
    PUSH32 esi
    call cpu_alu_%2
    POP32 esi
    mov edx, eax
    POP32 eax
    jmp cpu_write_rmw
%endmacro

%macro CPU_REGISTER_OP 4
cpu_op_%1:
    call cpu_idle_irq
    call cpu_width_%4
    CPU_LOAD_REG %3
    call cpu_alu_%2
    CPU_STORE_REG %3
    ret
%endmacro

%macro CPU_TRANSFER 4
cpu_op_%1:
    call cpu_idle_irq
%ifidni %4, word
    mov ebx, 2
%else
    call cpu_width_%4
%endif
    movzx eax, word [%2]
    CPU_STORE_REG %3
    jmp cpu_set_nz
%endmacro

%macro CPU_SET_FLAG 3
cpu_op_%1:
%ifidni %1, 58
    call cpu_poll_irq_before_p
%elifidni %1, 78
    call cpu_poll_irq_before_p
%endif
    call cpu_idle_irq
    %2 byte [cpu_p], %3
    ret
%endmacro

%macro CPU_BRANCH 3
cpu_op_%1:
    call cpu_fetch8
    test byte [cpu_p], %2
    %3 cpu_branch_take
    ret
%endmacro

cpu_branch_take:
    movsx eax, al
    movzx edx, word [cpu_pc]
    add eax, edx
    cmp byte [cpu_e], 0
    je .commit
    xor edx, eax
    test edx, 0xff00
    jz .commit
    call cpu_idle
.commit:
    call cpu_idle
    mov [cpu_pc], ax
    ret

%macro CPU_PUSH_REG 3
cpu_op_%1:
    call cpu_idle
    movzx eax, word [%2]
%ifidni %3, byte
    jmp cpu_push8
%elifidni %3, word
    call cpu_push16_native
    jmp cpu_force_stack_page
%else
    call cpu_width_%3
    cmp ebx, 1
    je cpu_push8
    jmp cpu_push16
%endif
%endmacro

%macro CPU_PULL_REG 3
cpu_op_%1:
    call cpu_idle
    call cpu_idle
%ifidni %3, byte
    mov ebx, 1
    call cpu_pull8_native
    call cpu_force_stack_page
%elifidni %3, word
    mov ebx, 2
    call cpu_pull16_native
    call cpu_force_stack_page
%else
    call cpu_width_%3
    cmp ebx, 1
    je %%byte
    call cpu_pull16
    jmp %%store
%%byte:
    call cpu_pull8
%%store:
%endif
    CPU_STORE_REG %2
    jmp cpu_set_nz
%endmacro

%macro CPU_SOFTWARE_INTERRUPT 3
cpu_op_%1:
    call cpu_fetch8
    mov ebx, %2
    cmp byte [cpu_e], 0
    je %%native
    mov ebx, %3
%%native:
    mov ebp, CPU_XF
    jmp cpu_push_interrupt
%endmacro

%macro CPU_MOVE_BLOCK 2
cpu_op_%1:
    call cpu_fetch8
    mov [cpu_dbr], al
    mov ebx, eax
    call cpu_fetch8
    shl eax, 16
    mov ax, [cpu_x]
    call bus_read
    mov edx, eax
    mov eax, ebx
    shl eax, 16
    mov ax, [cpu_y]
    call bus_write
    call cpu_idle
    test byte [cpu_p], CPU_XF
    jnz %%index_byte
    %2 word [cpu_x]
    %2 word [cpu_y]
    jmp %%count
%%index_byte:
    %2 byte [cpu_x]
    %2 byte [cpu_y]
%%count:
    call cpu_idle
    sub word [cpu_a], 1
    jc %%done
    sub word [cpu_pc], 3
%%done:
    ret
%endmacro

CPU_SOFTWARE_INTERRUPT 00, 0xffe6, 0xfffe
CPU_SOFTWARE_INTERRUPT 02, 0xffe4, 0xfff4

cpu_op_08:                      ; PHP
    call cpu_idle
    movzx eax, byte [cpu_p]
    jmp cpu_push8

CPU_PUSH_REG 0b, cpu_d, word

cpu_op_1b:                      ; TCS
    call cpu_idle_irq
    mov ax, [cpu_a]
    mov [cpu_s], ax
    jmp cpu_force_stack_page

cpu_op_20:                      ; JSR abs
    call cpu_fetch16
    mov ebx, eax
    call cpu_idle
    movzx eax, word [cpu_pc]
    dec eax
    call cpu_push16
    mov [cpu_pc], bx
    ret

cpu_op_22:                      ; JSL long
    call cpu_fetch16
    mov ebx, eax
    movzx eax, byte [cpu_pbr]
    call cpu_push8_native
    call cpu_idle
    call cpu_fetch8
    mov ecx, eax
    movzx eax, word [cpu_pc]
    dec eax
    call cpu_push16_native
    mov [cpu_pbr], cl
    mov [cpu_pc], bx
    jmp cpu_force_stack_page

cpu_op_28:                      ; PLP
    call cpu_idle
    call cpu_idle
    call cpu_poll_irq_before_p
    call cpu_pull8
    mov [cpu_p], al
    jmp cpu_normalize_p

CPU_PULL_REG 2b, cpu_d, word
CPU_TRANSFER 3b, cpu_s, cpu_a, word

cpu_op_40:                      ; RTI
    call cpu_idle
    call cpu_idle
    call cpu_pull8
    mov [cpu_p], al
    call cpu_normalize_p
    call cpu_pull16
    mov [cpu_pc], ax
    cmp byte [cpu_e], 0
    jne .done
    call cpu_pull8
    mov [cpu_pbr], al
.done:
    ret

cpu_op_42:                      ; WDM signature
    jmp cpu_fetch8

CPU_MOVE_BLOCK 44, dec
CPU_PUSH_REG 48, cpu_a, a
CPU_PUSH_REG 4b, cpu_pbr, byte

cpu_op_4c:                      ; JMP abs
    call cpu_fetch16
    mov [cpu_pc], ax
    ret

CPU_MOVE_BLOCK 54, inc
CPU_PUSH_REG 5a, cpu_y, x
CPU_TRANSFER 5b, cpu_a, cpu_d, word

cpu_op_5c:                      ; JML long
    call cpu_fetch24
    jmp cpu_jump_long

cpu_op_60:                      ; RTS
    call cpu_idle
    call cpu_idle
    call cpu_pull16
    call cpu_idle
    inc eax
    mov [cpu_pc], ax
    ret

cpu_op_62:                      ; PER
    call cpu_fetch16
    call cpu_idle
    add ax, [cpu_pc]
    call cpu_push16_native
    jmp cpu_force_stack_page

CPU_PULL_REG 68, cpu_a, a

cpu_op_6b:                      ; RTL
    call cpu_idle
    call cpu_idle
    call cpu_pull16_native
    mov ebx, eax
    call cpu_pull8_native
    mov [cpu_pbr], al
    inc ebx
    mov [cpu_pc], bx
    jmp cpu_force_stack_page

cpu_op_6c:                      ; JMP (abs)
    call cpu_fetch16
    mov esi, 0xffff
    call cpu_read16
    mov [cpu_pc], ax
    ret

CPU_PULL_REG 7a, cpu_y, x
CPU_TRANSFER 7b, cpu_d, cpu_a, word

cpu_op_7c:                      ; JMP (abs,X)
    call cpu_fetch16
    call cpu_idle
    call cpu_read_pbr_index
    mov [cpu_pc], ax
    ret

cpu_op_80:                      ; BRA
    call cpu_fetch8
    jmp cpu_branch_take

cpu_op_82:                      ; BRL
    call cpu_fetch16
    call cpu_idle
    add [cpu_pc], ax
    ret

CPU_TRANSFER 8a, cpu_x, cpu_a, a
CPU_PUSH_REG 8b, cpu_dbr, byte
CPU_TRANSFER 98, cpu_y, cpu_a, a

cpu_op_9a:                      ; TXS
    call cpu_idle_irq
    mov ax, [cpu_x]
    mov [cpu_s], ax
    jmp cpu_force_stack_page

CPU_TRANSFER 9b, cpu_x, cpu_y, x
CPU_TRANSFER a8, cpu_a, cpu_y, x
CPU_TRANSFER aa, cpu_a, cpu_x, x
CPU_PULL_REG ab, cpu_dbr, byte
CPU_TRANSFER ba, cpu_s, cpu_x, x
CPU_TRANSFER bb, cpu_y, cpu_x, x

cpu_op_c2:                      ; REP
    call cpu_fetch8
    call cpu_poll_irq_before_p
    call cpu_idle
    not eax
    and [cpu_p], al
    jmp cpu_normalize_p

cpu_op_cb:                      ; WAI
    call cpu_idle
    mov byte [cpu_waiting], 1
    jmp cpu_idle

cpu_op_d4:                      ; PEI (dp)
    call cpu_addr_dp_start
    call cpu_addr_direct_native
    call cpu_read16
    call cpu_push16_native
    jmp cpu_force_stack_page

CPU_PUSH_REG da, cpu_x, x

cpu_op_db:                      ; STP
    call cpu_idle
    mov byte [cpu_stopped], 1
    jmp cpu_idle

cpu_op_dc:                      ; JML [abs]
    call cpu_fetch16
    mov esi, 0xffff
    call cpu_read24
cpu_jump_long:
    mov [cpu_pc], ax
    shr eax, 16
    mov [cpu_pbr], al
    ret

cpu_op_e2:                      ; SEP
    call cpu_fetch8
    call cpu_poll_irq_before_p
    call cpu_idle
    or [cpu_p], al
    jmp cpu_normalize_p

cpu_op_ea:                      ; NOP
    jmp cpu_idle_irq

cpu_op_eb:                      ; XBA
    call cpu_idle
    call cpu_idle
    movzx eax, word [cpu_a]
    rol ax, 8
    mov [cpu_a], ax
    mov ebx, 1
    jmp cpu_set_nz

cpu_op_f4:                      ; PEA
    call cpu_fetch16
    call cpu_push16_native
    jmp cpu_force_stack_page

CPU_PULL_REG fa, cpu_x, x

cpu_op_fb:                      ; XCE
    call cpu_idle_irq
    movzx eax, byte [cpu_p]
    and eax, CPU_C
    movzx edx, byte [cpu_e]
    mov [cpu_e], al
    and byte [cpu_p], ~CPU_C
    or [cpu_p], dl
    jmp cpu_normalize_p

cpu_op_fc:                      ; JSR (abs,X)
    call cpu_fetch8
    mov ebx, eax
    movzx eax, word [cpu_pc]
    call cpu_push16_native
    call cpu_fetch8
    shl eax, 8
    or eax, ebx
    call cpu_idle
    call cpu_read_pbr_index
    mov [cpu_pc], ax
    jmp cpu_force_stack_page

cpu_read_pbr_index:
    add ax, [cpu_x]
    movzx edx, byte [cpu_pbr]
    shl edx, 16
    or eax, edx
    mov esi, 0xffff
    jmp cpu_read16

; Each row matches the architectural opcode grid.

; 0x00-0x0F
CPU_READ_OP 01, ora, dpix, a
CPU_READ_OP 03, ora, sr, a
CPU_RMW_OP 04, tsb, dp
CPU_READ_OP 05, ora, dp, a
CPU_RMW_OP 06, asl, dp
CPU_READ_OP 07, ora, dpl, a
CPU_READ_OP 09, ora, imm, a
CPU_REGISTER_OP 0a, asl, cpu_a, a
CPU_RMW_OP 0c, tsb, abs
CPU_READ_OP 0d, ora, abs, a
CPU_RMW_OP 0e, asl, abs
CPU_READ_OP 0f, ora, long, a

; 0x10-0x1F
CPU_BRANCH 10, CPU_N, jz
CPU_READ_OP 11, ora, dpiy, a
CPU_READ_OP 12, ora, dpi, a
CPU_READ_OP 13, ora, sriy, a
CPU_RMW_OP 14, trb, dp
CPU_READ_OP 15, ora, dpx, a
CPU_RMW_OP 16, asl, dpx
CPU_READ_OP 17, ora, dply, a
CPU_SET_FLAG 18, and, ~CPU_C
CPU_READ_OP 19, ora, absy, a
CPU_REGISTER_OP 1a, inc, cpu_a, a
CPU_RMW_OP 1c, trb, abs
CPU_READ_OP 1d, ora, absx, a
CPU_RMW_OP 1e, asl, absx
CPU_READ_OP 1f, ora, longx, a

; 0x20-0x2F
CPU_READ_OP 21, and, dpix, a
CPU_READ_OP 23, and, sr, a
CPU_READ_OP 24, bit, dp, a
CPU_READ_OP 25, and, dp, a
CPU_RMW_OP 26, rol, dp
CPU_READ_OP 27, and, dpl, a
CPU_READ_OP 29, and, imm, a
CPU_REGISTER_OP 2a, rol, cpu_a, a
CPU_READ_OP 2c, bit, abs, a
CPU_READ_OP 2d, and, abs, a
CPU_RMW_OP 2e, rol, abs
CPU_READ_OP 2f, and, long, a

; 0x30-0x3F
CPU_BRANCH 30, CPU_N, jnz
CPU_READ_OP 31, and, dpiy, a
CPU_READ_OP 32, and, dpi, a
CPU_READ_OP 33, and, sriy, a
CPU_READ_OP 34, bit, dpx, a
CPU_READ_OP 35, and, dpx, a
CPU_RMW_OP 36, rol, dpx
CPU_READ_OP 37, and, dply, a
CPU_SET_FLAG 38, or, CPU_C
CPU_READ_OP 39, and, absy, a
CPU_REGISTER_OP 3a, dec, cpu_a, a
CPU_READ_OP 3c, bit, absx, a
CPU_READ_OP 3d, and, absx, a
CPU_RMW_OP 3e, rol, absx
CPU_READ_OP 3f, and, longx, a

; 0x40-0x4F
CPU_READ_OP 41, eor, dpix, a
CPU_READ_OP 43, eor, sr, a
CPU_READ_OP 45, eor, dp, a
CPU_RMW_OP 46, lsr, dp
CPU_READ_OP 47, eor, dpl, a
CPU_READ_OP 49, eor, imm, a
CPU_REGISTER_OP 4a, lsr, cpu_a, a
CPU_READ_OP 4d, eor, abs, a
CPU_RMW_OP 4e, lsr, abs
CPU_READ_OP 4f, eor, long, a

; 0x50-0x5F
CPU_BRANCH 50, CPU_V, jz
CPU_READ_OP 51, eor, dpiy, a
CPU_READ_OP 52, eor, dpi, a
CPU_READ_OP 53, eor, sriy, a
CPU_READ_OP 55, eor, dpx, a
CPU_RMW_OP 56, lsr, dpx
CPU_READ_OP 57, eor, dply, a
CPU_SET_FLAG 58, and, ~CPU_I
CPU_READ_OP 59, eor, absy, a
CPU_READ_OP 5d, eor, absx, a
CPU_RMW_OP 5e, lsr, absx
CPU_READ_OP 5f, eor, longx, a

; 0x60-0x6F
CPU_READ_OP 61, adc, dpix, a
CPU_READ_OP 63, adc, sr, a
CPU_WRITE_OP 64, zero, dp, a
CPU_READ_OP 65, adc, dp, a
CPU_RMW_OP 66, ror, dp
CPU_READ_OP 67, adc, dpl, a
CPU_READ_OP 69, adc, imm, a
CPU_REGISTER_OP 6a, ror, cpu_a, a
CPU_READ_OP 6d, adc, abs, a
CPU_RMW_OP 6e, ror, abs
CPU_READ_OP 6f, adc, long, a

; 0x70-0x7F
CPU_BRANCH 70, CPU_V, jnz
CPU_READ_OP 71, adc, dpiy, a
CPU_READ_OP 72, adc, dpi, a
CPU_READ_OP 73, adc, sriy, a
CPU_WRITE_OP 74, zero, dpx, a
CPU_READ_OP 75, adc, dpx, a
CPU_RMW_OP 76, ror, dpx
CPU_READ_OP 77, adc, dply, a
CPU_SET_FLAG 78, or, CPU_I
CPU_READ_OP 79, adc, absy, a
CPU_READ_OP 7d, adc, absx, a
CPU_RMW_OP 7e, ror, absx
CPU_READ_OP 7f, adc, longx, a

; 0x80-0x8F
CPU_WRITE_OP 81, cpu_a, dpix, a
CPU_WRITE_OP 83, cpu_a, sr, a
CPU_WRITE_OP 84, cpu_y, dp, x
CPU_WRITE_OP 85, cpu_a, dp, a
CPU_WRITE_OP 86, cpu_x, dp, x
CPU_WRITE_OP 87, cpu_a, dpl, a
CPU_REGISTER_OP 88, dec, cpu_y, x
CPU_READ_OP 89, biti, imm, a
CPU_WRITE_OP 8c, cpu_y, abs, x
CPU_WRITE_OP 8d, cpu_a, abs, a
CPU_WRITE_OP 8e, cpu_x, abs, x
CPU_WRITE_OP 8f, cpu_a, long, a

; 0x90-0x9F
CPU_BRANCH 90, CPU_C, jz
CPU_WRITE_OP 91, cpu_a, dpiy, a
CPU_WRITE_OP 92, cpu_a, dpi, a
CPU_WRITE_OP 93, cpu_a, sriy, a
CPU_WRITE_OP 94, cpu_y, dpx, x
CPU_WRITE_OP 95, cpu_a, dpx, a
CPU_WRITE_OP 96, cpu_x, dpy, x
CPU_WRITE_OP 97, cpu_a, dply, a
CPU_WRITE_OP 99, cpu_a, absy, a
CPU_WRITE_OP 9c, zero, abs, a
CPU_WRITE_OP 9d, cpu_a, absx, a
CPU_WRITE_OP 9e, zero, absx, a
CPU_WRITE_OP 9f, cpu_a, longx, a

; 0xa0-0xaF
CPU_READ_OP a0, ldy, imm, x
CPU_READ_OP a1, lda, dpix, a
CPU_READ_OP a2, ldx, imm, x
CPU_READ_OP a3, lda, sr, a
CPU_READ_OP a4, ldy, dp, x
CPU_READ_OP a5, lda, dp, a
CPU_READ_OP a6, ldx, dp, x
CPU_READ_OP a7, lda, dpl, a
CPU_READ_OP a9, lda, imm, a
CPU_READ_OP ac, ldy, abs, x
CPU_READ_OP ad, lda, abs, a
CPU_READ_OP ae, ldx, abs, x
CPU_READ_OP af, lda, long, a

; 0xb0-0xbF
CPU_BRANCH b0, CPU_C, jnz
CPU_READ_OP b1, lda, dpiy, a
CPU_READ_OP b2, lda, dpi, a
CPU_READ_OP b3, lda, sriy, a
CPU_READ_OP b4, ldy, dpx, x
CPU_READ_OP b5, lda, dpx, a
CPU_READ_OP b6, ldx, dpy, x
CPU_READ_OP b7, lda, dply, a
CPU_SET_FLAG b8, and, ~CPU_V
CPU_READ_OP b9, lda, absy, a
CPU_READ_OP bc, ldy, absx, x
CPU_READ_OP bd, lda, absx, a
CPU_READ_OP be, ldx, absy, x
CPU_READ_OP bf, lda, longx, a

; 0xc0-0xcF
CPU_READ_OP c0, cpy, imm, x
CPU_READ_OP c1, cmp, dpix, a
CPU_READ_OP c3, cmp, sr, a
CPU_READ_OP c4, cpy, dp, x
CPU_READ_OP c5, cmp, dp, a
CPU_RMW_OP c6, dec, dp
CPU_READ_OP c7, cmp, dpl, a
CPU_REGISTER_OP c8, inc, cpu_y, x
CPU_READ_OP c9, cmp, imm, a
CPU_REGISTER_OP ca, dec, cpu_x, x
CPU_READ_OP cc, cpy, abs, x
CPU_READ_OP cd, cmp, abs, a
CPU_RMW_OP ce, dec, abs
CPU_READ_OP cf, cmp, long, a

; 0xd0-0xdF
CPU_BRANCH d0, CPU_Z, jz
CPU_READ_OP d1, cmp, dpiy, a
CPU_READ_OP d2, cmp, dpi, a
CPU_READ_OP d3, cmp, sriy, a
CPU_READ_OP d5, cmp, dpx, a
CPU_RMW_OP d6, dec, dpx
CPU_READ_OP d7, cmp, dply, a
CPU_SET_FLAG d8, and, ~CPU_DF
CPU_READ_OP d9, cmp, absy, a
CPU_READ_OP dd, cmp, absx, a
CPU_RMW_OP de, dec, absx
CPU_READ_OP df, cmp, longx, a

; 0xe0-0xeF
CPU_READ_OP e0, cpx, imm, x
CPU_READ_OP e1, sbc, dpix, a
CPU_READ_OP e3, sbc, sr, a
CPU_READ_OP e4, cpx, dp, x
CPU_READ_OP e5, sbc, dp, a
CPU_RMW_OP e6, inc, dp
CPU_READ_OP e7, sbc, dpl, a
CPU_REGISTER_OP e8, inc, cpu_x, x
CPU_READ_OP e9, sbc, imm, a
CPU_READ_OP ec, cpx, abs, x
CPU_READ_OP ed, sbc, abs, a
CPU_RMW_OP ee, inc, abs
CPU_READ_OP ef, sbc, long, a

; 0xf0-0xfF
CPU_BRANCH f0, CPU_Z, jnz
CPU_READ_OP f1, sbc, dpiy, a
CPU_READ_OP f2, sbc, dpi, a
CPU_READ_OP f3, sbc, sriy, a
CPU_READ_OP f5, sbc, dpx, a
CPU_RMW_OP f6, inc, dpx
CPU_READ_OP f7, sbc, dply, a
CPU_SET_FLAG f8, or, CPU_DF
CPU_READ_OP f9, sbc, absy, a
CPU_READ_OP fd, sbc, absx, a
CPU_RMW_OP fe, inc, absx
CPU_READ_OP ff, sbc, longx, a

align 8
cpu_dispatch:
%if __BITS__ = 64
    %define CPU_ENTRY dq
%else
    %define CPU_ENTRY dd
%endif
    CPU_ENTRY cpu_op_00, cpu_op_01, cpu_op_02, cpu_op_03, cpu_op_04, cpu_op_05, cpu_op_06, cpu_op_07
    CPU_ENTRY cpu_op_08, cpu_op_09, cpu_op_0a, cpu_op_0b, cpu_op_0c, cpu_op_0d, cpu_op_0e, cpu_op_0f
    CPU_ENTRY cpu_op_10, cpu_op_11, cpu_op_12, cpu_op_13, cpu_op_14, cpu_op_15, cpu_op_16, cpu_op_17
    CPU_ENTRY cpu_op_18, cpu_op_19, cpu_op_1a, cpu_op_1b, cpu_op_1c, cpu_op_1d, cpu_op_1e, cpu_op_1f
    CPU_ENTRY cpu_op_20, cpu_op_21, cpu_op_22, cpu_op_23, cpu_op_24, cpu_op_25, cpu_op_26, cpu_op_27
    CPU_ENTRY cpu_op_28, cpu_op_29, cpu_op_2a, cpu_op_2b, cpu_op_2c, cpu_op_2d, cpu_op_2e, cpu_op_2f
    CPU_ENTRY cpu_op_30, cpu_op_31, cpu_op_32, cpu_op_33, cpu_op_34, cpu_op_35, cpu_op_36, cpu_op_37
    CPU_ENTRY cpu_op_38, cpu_op_39, cpu_op_3a, cpu_op_3b, cpu_op_3c, cpu_op_3d, cpu_op_3e, cpu_op_3f
    CPU_ENTRY cpu_op_40, cpu_op_41, cpu_op_42, cpu_op_43, cpu_op_44, cpu_op_45, cpu_op_46, cpu_op_47
    CPU_ENTRY cpu_op_48, cpu_op_49, cpu_op_4a, cpu_op_4b, cpu_op_4c, cpu_op_4d, cpu_op_4e, cpu_op_4f
    CPU_ENTRY cpu_op_50, cpu_op_51, cpu_op_52, cpu_op_53, cpu_op_54, cpu_op_55, cpu_op_56, cpu_op_57
    CPU_ENTRY cpu_op_58, cpu_op_59, cpu_op_5a, cpu_op_5b, cpu_op_5c, cpu_op_5d, cpu_op_5e, cpu_op_5f
    CPU_ENTRY cpu_op_60, cpu_op_61, cpu_op_62, cpu_op_63, cpu_op_64, cpu_op_65, cpu_op_66, cpu_op_67
    CPU_ENTRY cpu_op_68, cpu_op_69, cpu_op_6a, cpu_op_6b, cpu_op_6c, cpu_op_6d, cpu_op_6e, cpu_op_6f
    CPU_ENTRY cpu_op_70, cpu_op_71, cpu_op_72, cpu_op_73, cpu_op_74, cpu_op_75, cpu_op_76, cpu_op_77
    CPU_ENTRY cpu_op_78, cpu_op_79, cpu_op_7a, cpu_op_7b, cpu_op_7c, cpu_op_7d, cpu_op_7e, cpu_op_7f
    CPU_ENTRY cpu_op_80, cpu_op_81, cpu_op_82, cpu_op_83, cpu_op_84, cpu_op_85, cpu_op_86, cpu_op_87
    CPU_ENTRY cpu_op_88, cpu_op_89, cpu_op_8a, cpu_op_8b, cpu_op_8c, cpu_op_8d, cpu_op_8e, cpu_op_8f
    CPU_ENTRY cpu_op_90, cpu_op_91, cpu_op_92, cpu_op_93, cpu_op_94, cpu_op_95, cpu_op_96, cpu_op_97
    CPU_ENTRY cpu_op_98, cpu_op_99, cpu_op_9a, cpu_op_9b, cpu_op_9c, cpu_op_9d, cpu_op_9e, cpu_op_9f
    CPU_ENTRY cpu_op_a0, cpu_op_a1, cpu_op_a2, cpu_op_a3, cpu_op_a4, cpu_op_a5, cpu_op_a6, cpu_op_a7
    CPU_ENTRY cpu_op_a8, cpu_op_a9, cpu_op_aa, cpu_op_ab, cpu_op_ac, cpu_op_ad, cpu_op_ae, cpu_op_af
    CPU_ENTRY cpu_op_b0, cpu_op_b1, cpu_op_b2, cpu_op_b3, cpu_op_b4, cpu_op_b5, cpu_op_b6, cpu_op_b7
    CPU_ENTRY cpu_op_b8, cpu_op_b9, cpu_op_ba, cpu_op_bb, cpu_op_bc, cpu_op_bd, cpu_op_be, cpu_op_bf
    CPU_ENTRY cpu_op_c0, cpu_op_c1, cpu_op_c2, cpu_op_c3, cpu_op_c4, cpu_op_c5, cpu_op_c6, cpu_op_c7
    CPU_ENTRY cpu_op_c8, cpu_op_c9, cpu_op_ca, cpu_op_cb, cpu_op_cc, cpu_op_cd, cpu_op_ce, cpu_op_cf
    CPU_ENTRY cpu_op_d0, cpu_op_d1, cpu_op_d2, cpu_op_d3, cpu_op_d4, cpu_op_d5, cpu_op_d6, cpu_op_d7
    CPU_ENTRY cpu_op_d8, cpu_op_d9, cpu_op_da, cpu_op_db, cpu_op_dc, cpu_op_dd, cpu_op_de, cpu_op_df
    CPU_ENTRY cpu_op_e0, cpu_op_e1, cpu_op_e2, cpu_op_e3, cpu_op_e4, cpu_op_e5, cpu_op_e6, cpu_op_e7
    CPU_ENTRY cpu_op_e8, cpu_op_e9, cpu_op_ea, cpu_op_eb, cpu_op_ec, cpu_op_ed, cpu_op_ee, cpu_op_ef
    CPU_ENTRY cpu_op_f0, cpu_op_f1, cpu_op_f2, cpu_op_f3, cpu_op_f4, cpu_op_f5, cpu_op_f6, cpu_op_f7
    CPU_ENTRY cpu_op_f8, cpu_op_f9, cpu_op_fa, cpu_op_fb, cpu_op_fc, cpu_op_fd, cpu_op_fe, cpu_op_ff
%undef CPU_ENTRY
