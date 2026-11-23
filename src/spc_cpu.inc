; SPC700 instruction timing and semantics. The APU supplies the memory bus.

%define SPC_C 0x01
%define SPC_Z 0x02
%define SPC_I 0x04
%define SPC_H 0x08
%define SPC_B 0x10
%define SPC_D 0x20
%define SPC_V 0x40
%define SPC_N 0x80

%macro SPC_CPU_STATE 0
spc_pc:      resw 1
spc_a:       resb 1
spc_x:       resb 1
spc_y:       resb 1
spc_s:       resb 1
spc_p:       resb 1
spc_stopped: resb 1
spc_cycles:  resb 1
spc_fault:   resd 1
%endmacro

spc_cpu_reset:
    mov word [spc_pc], 0xffc0
    mov byte [spc_s], 0xef
    mov byte [spc_p], SPC_Z
    ret

spc_step:
    cmp byte [spc_stopped], 0
    jne .halt
    mov byte [spc_cycles], 0
    call spc_fetch
%if __BITS__ = 64
    call [spc_dispatch + eax * 8]
%else
    call [spc_dispatch + eax * 4]
%endif
    movzx eax, byte [spc_cycles]
    ret
.halt:
    xor eax, eax
    ret

spc_cpu_read:
    inc byte [spc_cycles]
    jmp spc_read

spc_cpu_write:
    inc byte [spc_cycles]
    jmp spc_write

spc_cpu_idle:
    inc byte [spc_cycles]
    ret

spc_fetch:
    movzx eax, word [spc_pc]
    inc word [spc_pc]
    jmp spc_cpu_read

spc_fetch16:
    call spc_fetch
    mov ecx, eax
    call spc_fetch
    shl eax, 8
    or eax, ecx
    ret

spc_dummy_pc:
    movzx eax, word [spc_pc]
    jmp spc_cpu_read

spc_direct:
    and eax, 255
    test byte [spc_p], SPC_D
    jz .done
    or eax, 256
.done:
    ret

spc_read_dp:
    call spc_direct
    jmp spc_cpu_read

spc_write_dp:
    call spc_direct
    jmp spc_cpu_write

spc_addr_dp:
    call spc_fetch
    jmp spc_direct

spc_addr_dpx:
    call spc_fetch
    movzx ecx, byte [spc_x]
    jmp spc_addr_index_dp
spc_addr_dpy:
    call spc_fetch
    movzx ecx, byte [spc_y]
spc_addr_index_dp:
    call spc_cpu_idle
    add eax, ecx
    jmp spc_direct

spc_addr_abs:
    jmp spc_fetch16

spc_addr_absx:
    call spc_fetch16
    movzx ecx, byte [spc_x]
    jmp spc_addr_index_abs
spc_addr_absy:
    call spc_fetch16
    movzx ecx, byte [spc_y]
spc_addr_index_abs:
    call spc_cpu_idle
    add ax, cx
    ret

spc_addr_indx:
    call spc_dummy_pc
    movzx eax, byte [spc_x]
    jmp spc_direct

spc_addr_dpix:
    call spc_fetch
    call spc_cpu_idle
    movzx ecx, byte [spc_x]
    add eax, ecx
    jmp spc_dp_pointer

spc_addr_dpiy:
    call spc_fetch
    call spc_cpu_idle
    call spc_dp_pointer
    movzx ecx, byte [spc_y]
    add ax, cx
    ret

spc_dp_pointer:
    mov ebp, eax
    call spc_read_dp
    mov ecx, eax
    lea eax, [ebp + 1]
    call spc_read_dp
    shl eax, 8
    or eax, ecx
    ret

spc_read16:
    mov ebp, eax
    call spc_cpu_read
    mov ecx, eax
    mov eax, ebp
    inc ax
    call spc_cpu_read
    shl eax, 8
    or eax, ecx
    ret

spc_push:
    mov edx, eax
    movzx eax, byte [spc_s]
    or eax, 256
    call spc_cpu_write
    dec byte [spc_s]
    ret

spc_pull:
    inc byte [spc_s]
    movzx eax, byte [spc_s]
    or eax, 256
    jmp spc_cpu_read

spc_push_pc:
    movzx eax, byte [spc_pc + 1]
    call spc_push
    movzx eax, byte [spc_pc]
    jmp spc_push

spc_pull_pc:
    call spc_pull
    mov ecx, eax
    call spc_pull
    shl eax, 8
    or eax, ecx
    mov [spc_pc], ax
    ret

spc_branch_take:
    call spc_cpu_idle
    call spc_cpu_idle
    movsx eax, al
    add [spc_pc], ax
    ret

; Binary ALU operations accept CL = left operand, AL = right.
; Single-operand operations accept AL, with AL also holding the result.
spc_alu_mov:
    jmp spc_nz

%macro SPC_LOGIC 2
spc_alu_%1:
    %2 al, cl
    jmp spc_nz
%endmacro

SPC_LOGIC or, or
SPC_LOGIC and, and
SPC_LOGIC eor, xor

spc_alu_cmp:
    mov edx, ecx
    and byte [spc_p], ~SPC_C
    sub dl, al
    jc .carry
    or byte [spc_p], SPC_C
.carry:
    mov al, dl
    call spc_nz
    mov al, cl
    ret

spc_alu_sbc:
    not al
spc_alu_adc:
    movzx edx, byte [spc_p]
    bt edx, 0
    adc al, cl
    setc dl
    seto dh
    lahf
    and byte [spc_p], ~(SPC_C | SPC_V | SPC_H)
    or [spc_p], dl
    test dh, dh
    jz .half
    or byte [spc_p], SPC_V
.half:
    and ah, 0x10
    shr ah, 1
    or [spc_p], ah
    jmp spc_nz

%macro SPC_SHIFT 2
spc_alu_%1:
    movzx edx, byte [spc_p]
    bt edx, 0
    %2 al, 1
    setc dl
    and byte [spc_p], ~SPC_C
    or [spc_p], dl
    jmp spc_nz
%endmacro

SPC_SHIFT asl, shl
SPC_SHIFT lsr, shr
SPC_SHIFT rol, rcl
SPC_SHIFT ror, rcr

spc_alu_inc:
    inc al
    jmp spc_nz

spc_alu_dec:
    dec al
    jmp spc_nz

spc_nz16:
    and byte [spc_p], ~(SPC_Z | SPC_N)
    test ax, ax
    jmp spc_nz_result

spc_nz:
    and byte [spc_p], ~(SPC_Z | SPC_N)
    test al, al
spc_nz_result:
    js .negative
    jnz .done
    or byte [spc_p], SPC_Z
.done:
    ret
.negative:
    or byte [spc_p], SPC_N
    ret

%macro SPC_READ 4
spc_op_%1:
%ifidni %3, imm
    call spc_fetch
%else
    call spc_addr_%3
    call spc_cpu_read
%endif
    mov cl, [%4]
    call spc_alu_%2
    mov [%4], al
    ret
%endmacro

%macro SPC_STORE 3
spc_op_%1:
    call spc_addr_%2
    mov ebx, eax
    call spc_cpu_read
    mov eax, ebx
    movzx edx, byte [%3]
    jmp spc_cpu_write
%endmacro

%macro SPC_RMW 3
spc_op_%1:
    call spc_addr_%3
    mov ebx, eax
    call spc_cpu_read
    call spc_alu_%2
    mov edx, eax
    mov eax, ebx
    jmp spc_cpu_write
%endmacro

%macro SPC_REG_RMW 3
spc_op_%1:
    call spc_dummy_pc
    movzx eax, byte [%3]
    call spc_alu_%2
    mov [%3], al
    ret
%endmacro

%macro SPC_TRANSFER 3
spc_op_%1:
    call spc_dummy_pc
    movzx eax, byte [%2]
    mov [%3], al
%ifidni %3, spc_s
    ret
%else
    jmp spc_nz
%endif
%endmacro

%macro SPC_BRANCH 3
spc_op_%1:
    call spc_fetch
    test byte [spc_p], %2
    %3 spc_branch_take
    ret
%endmacro

%macro SPC_FLAG 3-4 0
spc_op_%1:
    call spc_dummy_pc
%if %4
    call spc_cpu_idle
%endif
    %2 byte [spc_p], %3
    ret
%endmacro

%macro SPC_PUSH 2
spc_op_%1:
    call spc_dummy_pc
    movzx eax, byte [%2]
    call spc_push
    jmp spc_cpu_idle
%endmacro

%macro SPC_PULL 2
spc_op_%1:
    call spc_dummy_pc
    call spc_cpu_idle
    call spc_pull
    mov [%2], al
    ret
%endmacro

%macro SPC_MEMORY_ALU 3
spc_op_%1:
%ifidni %3, dp
    call spc_fetch
    call spc_read_dp
    mov edi, eax
    call spc_addr_dp
%elifidni %3, imm
    call spc_fetch
    mov edi, eax
    call spc_addr_dp
%else
    call spc_dummy_pc
    movzx eax, byte [spc_y]
    call spc_read_dp
    mov edi, eax
    movzx eax, byte [spc_x]
    call spc_direct
%endif
    mov ebx, eax
    call spc_cpu_read
    mov ecx, eax
    mov eax, edi
    call spc_alu_%2
%ifidni %2, cmp
    jmp spc_cpu_idle
%else
    mov edx, eax
    mov eax, ebx
    jmp spc_cpu_write
%endif
%endmacro

%macro SPC_BITS 4
spc_op_%1:
    call spc_addr_dp
    mov ebx, eax
    call spc_cpu_read
    %2 al, %3
    mov edx, eax
    mov eax, ebx
    jmp spc_cpu_write
spc_op_%4:
    call spc_addr_dp
    call spc_cpu_read
    mov ebx, eax
    call spc_cpu_idle
    call spc_fetch
%ifidni %2, or
    test bl, %3
    jnz spc_branch_take
%else
    test bl, (~(%3)) & 0xff
    jz spc_branch_take
%endif
    ret
%endmacro

%macro SPC_TABLE_CALL 2
spc_op_%1:
    call spc_dummy_pc
    call spc_cpu_idle
    call spc_push_pc
    call spc_cpu_idle
    mov eax, 0xffde - %2 * 2
    call spc_read16
    mov [spc_pc], ax
    ret
%endmacro

spc_bit_address:
    call spc_fetch16
    mov ecx, eax
    shr ecx, 13
    and eax, 0x1fff
    mov ebx, eax
    call spc_cpu_read
    ret

%macro SPC_CARRY_BIT 4
spc_op_%1:
    call spc_bit_address
%if %4
    call spc_cpu_idle
%endif
    shr al, cl
    and al, 1
%if %3
    xor al, 1
%endif
%ifidni %2, mov
    and byte [spc_p], ~SPC_C
    or [spc_p], al
%elifidni %2, and
    or al, ~SPC_C
    and [spc_p], al
%else
    %2 [spc_p], al
%endif
    ret
%endmacro

SPC_CARRY_BIT 0a, or, 0, 1
SPC_CARRY_BIT 2a, or, 1, 1
SPC_CARRY_BIT 4a, and, 0, 0
SPC_CARRY_BIT 6a, and, 1, 0
SPC_CARRY_BIT 8a, xor, 0, 1
SPC_CARRY_BIT aa, mov, 0, 0

spc_op_ca:                          ; MOV1 mem.bit,C
    call spc_bit_address
    call spc_cpu_idle
    movzx edx, byte [spc_p]
    bt edx, 0
    jc .set
    btr eax, ecx
    jmp spc_write_bit
.set:
    bts eax, ecx
    jmp spc_write_bit

spc_op_ea:                          ; NOT1 mem.bit
    call spc_bit_address
    btc eax, ecx
spc_write_bit:
    mov edx, eax
    mov eax, ebx
    jmp spc_cpu_write

%macro SPC_TSET 2
spc_op_%1:
    call spc_fetch16
    mov ebx, eax
    call spc_cpu_read
    mov edi, eax
    movzx eax, byte [spc_a]
    mov edx, edi
    sub al, dl
    call spc_nz
    mov eax, ebx
    call spc_cpu_read
    movzx edx, byte [spc_a]
%if %2
    or edx, edi
%else
    not edx
    and edx, edi
%endif
    mov eax, ebx
    jmp spc_cpu_write
%endmacro

SPC_TSET 0e, 1
SPC_TSET 4e, 0

spc_op_00:                          ; NOP
    jmp spc_dummy_pc

spc_op_0f:                          ; BRK
    call spc_dummy_pc
    call spc_push_pc
    movzx eax, byte [spc_p]
    call spc_push
    call spc_cpu_idle
    mov eax, 0xffde
    call spc_read16
    mov [spc_pc], ax
    and byte [spc_p], ~SPC_I
    or byte [spc_p], SPC_B
    ret

spc_op_1f:                          ; JMP [abs+X]
    call spc_addr_absx
    call spc_read16
    mov [spc_pc], ax
    ret

spc_op_2e:                          ; CBNE dp,rel
    call spc_addr_dp
    jmp spc_branch_not_equal

spc_op_2f:                          ; BRA
    call spc_fetch
    jmp spc_branch_take

spc_op_3f:                          ; CALL abs
    call spc_fetch16
    mov ebx, eax
    call spc_cpu_idle
    call spc_push_pc
    call spc_cpu_idle
    call spc_cpu_idle
    mov [spc_pc], bx
    ret

spc_op_4f:                          ; PCALL
    call spc_fetch
    mov ebx, eax
    call spc_cpu_idle
    call spc_push_pc
    call spc_cpu_idle
    or ebx, 0xff00
    mov [spc_pc], bx
    ret

spc_op_5f:                          ; JMP abs
    call spc_fetch16
    mov [spc_pc], ax
    ret

spc_op_6e:                          ; DBNZ dp,rel
    call spc_addr_dp
    mov ebx, eax
    call spc_cpu_read
    dec al
    mov edx, eax
    mov eax, ebx
    call spc_cpu_write
    call spc_fetch
    test dl, dl
    jnz spc_branch_take
    ret

spc_op_6f:                          ; RET
    call spc_dummy_pc
    call spc_cpu_idle
    jmp spc_pull_pc

spc_op_7f:                          ; RETI
    call spc_dummy_pc
    call spc_cpu_idle
    call spc_pull
    mov [spc_p], al
    jmp spc_pull_pc

spc_op_8f:                          ; MOV dp,#imm
    call spc_fetch
    mov edx, eax
    call spc_addr_dp
    mov ebx, eax
    call spc_cpu_read
    mov eax, ebx
    jmp spc_cpu_write

spc_op_9e:                          ; DIV YA,X
    call spc_dummy_pc
    add byte [spc_cycles], 10
    movzx ecx, byte [spc_x]
    movzx ebx, byte [spc_y]
    mov eax, ebx
    shl eax, 8
    mov al, [spc_a]
    and byte [spc_p], ~(SPC_V | SPC_H)
    cmp ebx, ecx
    jb .half
    or byte [spc_p], SPC_V
.half:
    mov edx, ebx
    and edx, 15
    mov edi, ecx
    and edi, 15
    cmp edx, edi
    jb .divide
    or byte [spc_p], SPC_H
.divide:
    lea edx, [ecx + ecx]
    cmp ebx, edx
    jae .overflow
    xor edx, edx
    div ecx
    jmp .result
.overflow:
    mov edx, ecx
    shl edx, 9
    sub eax, edx
    mov ebx, 256
    sub ebx, ecx
    xor edx, edx
    div ebx
    not al
    add edx, ecx
.result:
    mov [spc_a], al
    mov [spc_y], dl
    jmp spc_nz

spc_op_9f:                          ; XCN
    call spc_dummy_pc
    add byte [spc_cycles], 3
    movzx eax, byte [spc_a]
    rol al, 4
    mov [spc_a], al
    jmp spc_nz

spc_op_af:                          ; MOV (X)+,A
    call spc_addr_indx
    call spc_cpu_idle
    inc byte [spc_x]
    movzx edx, byte [spc_a]
    jmp spc_cpu_write

spc_op_bf:                          ; MOV A,(X)+
    call spc_addr_indx
    inc byte [spc_x]
    call spc_cpu_read
    call spc_cpu_idle
    mov [spc_a], al
    jmp spc_nz

spc_op_cf:                          ; MUL YA
    call spc_dummy_pc
    add byte [spc_cycles], 7
    mov al, [spc_a]
    mul byte [spc_y]
    mov [spc_a], al
    mov [spc_y], ah
    mov al, ah
    jmp spc_nz

spc_op_d7:                          ; MOV [dp]+Y,A has a late idle cycle
    call spc_fetch
    call spc_dp_pointer
    call spc_cpu_idle
    movzx ecx, byte [spc_y]
    add ax, cx
    mov ebx, eax
    call spc_cpu_read
    mov eax, ebx
    movzx edx, byte [spc_a]
    jmp spc_cpu_write

spc_op_da:                          ; MOVW dp,YA
    call spc_fetch
    mov ebx, eax
    call spc_read_dp
    mov eax, ebx
    movzx edx, byte [spc_a]
    call spc_write_dp
    lea eax, [ebx + 1]
    movzx edx, byte [spc_y]
    jmp spc_write_dp

spc_op_de:                          ; CBNE dp+X,rel
    call spc_addr_dpx
spc_branch_not_equal:
    call spc_cpu_read
    mov ebx, eax
    call spc_cpu_idle
    call spc_fetch
    cmp bl, [spc_a]
    jne spc_branch_take
    ret

spc_op_be:                          ; DAS
    call spc_dummy_pc
    call spc_cpu_idle
    movzx eax, byte [spc_a]
    test byte [spc_p], SPC_C
    jz .high
    cmp al, 0x99
    jbe .low
.high:
    sub al, 0x60
    and byte [spc_p], ~SPC_C
.low:
    test byte [spc_p], SPC_H
    jz .adjust_low
    mov edx, eax
    and edx, 15
    cmp edx, 9
    jbe spc_decimal_done
.adjust_low:
    sub al, 6
    jmp spc_decimal_done

spc_op_df:                          ; DAA
    call spc_dummy_pc
    call spc_cpu_idle
    movzx eax, byte [spc_a]
    test byte [spc_p], SPC_C
    jnz .high
    cmp al, 0x99
    jbe .low
.high:
    add al, 0x60
    or byte [spc_p], SPC_C
.low:
    test byte [spc_p], SPC_H
    jnz .adjust_low
    mov edx, eax
    and edx, 15
    cmp edx, 9
    jbe spc_decimal_done
.adjust_low:
    add al, 6
spc_decimal_done:
    mov [spc_a], al
    jmp spc_nz

spc_op_ef:                          ; SLEEP
spc_op_ff:                          ; STOP
    call spc_dummy_pc
    call spc_cpu_idle
    mov byte [spc_stopped], 1
    ret

spc_op_fa:                          ; MOV dp,dp
    call spc_fetch
    call spc_read_dp
    mov edx, eax
    call spc_addr_dp
    jmp spc_cpu_write

spc_op_fe:                          ; DBNZ Y,rel
    call spc_dummy_pc
    call spc_cpu_idle
    call spc_fetch
    dec byte [spc_y]
    jnz spc_branch_take
    ret

%macro SPC_WORD_RMW 2
spc_op_%1:
    call spc_fetch
    mov ebx, eax
    call spc_read_dp
    add eax, %2
    mov edi, eax
    mov edx, eax
    mov eax, ebx
    call spc_write_dp
    lea eax, [ebx + 1]
    call spc_read_dp
    shl eax, 8
    add eax, edi
    mov edi, eax
    mov edx, eax
    shr edx, 8
    lea eax, [ebx + 1]
    call spc_write_dp
    mov eax, edi
    jmp spc_nz16
%endmacro

SPC_WORD_RMW 1a, -1
SPC_WORD_RMW 3a, 1

%macro SPC_WORD_READ 2
spc_op_%1:
    call spc_fetch
    mov ebx, eax
    call spc_read_dp
    mov edi, eax
%ifnidni %2, cmp
    call spc_cpu_idle
%endif
    lea eax, [ebx + 1]
    call spc_read_dp
    shl eax, 8
    or edi, eax
%ifidni %2, mov
    mov eax, edi
%elifidni %2, cmp
    movzx eax, byte [spc_y]
    shl eax, 8
    mov al, [spc_a]
    and byte [spc_p], ~SPC_C
    sub eax, edi
    jc %%compare_done
    or byte [spc_p], SPC_C
%%compare_done:
    jmp spc_nz16
%else
%ifidni %2, sbc
    or byte [spc_p], SPC_C
%else
    and byte [spc_p], ~SPC_C
%endif
    mov eax, edi
    mov cl, [spc_a]
    call spc_alu_%2
    mov [spc_a], al
    mov eax, edi
    shr eax, 8
    mov cl, [spc_y]
    call spc_alu_%2
    shl eax, 8
    mov al, [spc_a]
%endif
    mov [spc_a], al
    mov [spc_y], ah
    jmp spc_nz16
%endmacro

SPC_WORD_READ 5a, cmp
SPC_WORD_READ 7a, adc
SPC_WORD_READ 9a, sbc
SPC_WORD_READ ba, mov

; Opcode dispatch, in operand encoding order.

SPC_TABLE_CALL 01, 0
SPC_BITS 02, or, 1, 03
SPC_READ 04, or, dp, spc_a
SPC_READ 05, or, abs, spc_a
SPC_READ 06, or, indx, spc_a
SPC_READ 07, or, dpix, spc_a
SPC_READ 08, or, imm, spc_a
SPC_MEMORY_ALU 09, or, dp
SPC_RMW 0b, asl, dp
SPC_RMW 0c, asl, abs
SPC_PUSH 0d, spc_p

SPC_BRANCH 10, SPC_N, jz
SPC_TABLE_CALL 11, 1
SPC_BITS 12, and, 254, 13
SPC_READ 14, or, dpx, spc_a
SPC_READ 15, or, absx, spc_a
SPC_READ 16, or, absy, spc_a
SPC_READ 17, or, dpiy, spc_a
SPC_MEMORY_ALU 18, or, imm
SPC_MEMORY_ALU 19, or, xy
SPC_RMW 1b, asl, dpx
SPC_REG_RMW 1c, asl, spc_a
SPC_REG_RMW 1d, dec, spc_x
SPC_READ 1e, cmp, abs, spc_x

SPC_FLAG 20, and, ~SPC_D, 0
SPC_TABLE_CALL 21, 2
SPC_BITS 22, or, 2, 23
SPC_READ 24, and, dp, spc_a
SPC_READ 25, and, abs, spc_a
SPC_READ 26, and, indx, spc_a
SPC_READ 27, and, dpix, spc_a
SPC_READ 28, and, imm, spc_a
SPC_MEMORY_ALU 29, and, dp
SPC_RMW 2b, rol, dp
SPC_RMW 2c, rol, abs
SPC_PUSH 2d, spc_a

SPC_BRANCH 30, SPC_N, jnz
SPC_TABLE_CALL 31, 3
SPC_BITS 32, and, 253, 33
SPC_READ 34, and, dpx, spc_a
SPC_READ 35, and, absx, spc_a
SPC_READ 36, and, absy, spc_a
SPC_READ 37, and, dpiy, spc_a
SPC_MEMORY_ALU 38, and, imm
SPC_MEMORY_ALU 39, and, xy
SPC_RMW 3b, rol, dpx
SPC_REG_RMW 3c, rol, spc_a
SPC_REG_RMW 3d, inc, spc_x
SPC_READ 3e, cmp, dp, spc_x

SPC_FLAG 40, or, SPC_D, 0
SPC_TABLE_CALL 41, 4
SPC_BITS 42, or, 4, 43
SPC_READ 44, eor, dp, spc_a
SPC_READ 45, eor, abs, spc_a
SPC_READ 46, eor, indx, spc_a
SPC_READ 47, eor, dpix, spc_a
SPC_READ 48, eor, imm, spc_a
SPC_MEMORY_ALU 49, eor, dp
SPC_RMW 4b, lsr, dp
SPC_RMW 4c, lsr, abs
SPC_PUSH 4d, spc_x

SPC_BRANCH 50, SPC_V, jz
SPC_TABLE_CALL 51, 5
SPC_BITS 52, and, 251, 53
SPC_READ 54, eor, dpx, spc_a
SPC_READ 55, eor, absx, spc_a
SPC_READ 56, eor, absy, spc_a
SPC_READ 57, eor, dpiy, spc_a
SPC_MEMORY_ALU 58, eor, imm
SPC_MEMORY_ALU 59, eor, xy
SPC_RMW 5b, lsr, dpx
SPC_REG_RMW 5c, lsr, spc_a
SPC_TRANSFER 5d, spc_a, spc_x
SPC_READ 5e, cmp, abs, spc_y

SPC_FLAG 60, and, ~SPC_C, 0
SPC_TABLE_CALL 61, 6
SPC_BITS 62, or, 8, 63
SPC_READ 64, cmp, dp, spc_a
SPC_READ 65, cmp, abs, spc_a
SPC_READ 66, cmp, indx, spc_a
SPC_READ 67, cmp, dpix, spc_a
SPC_READ 68, cmp, imm, spc_a
SPC_MEMORY_ALU 69, cmp, dp
SPC_RMW 6b, ror, dp
SPC_RMW 6c, ror, abs
SPC_PUSH 6d, spc_y

SPC_BRANCH 70, SPC_V, jnz
SPC_TABLE_CALL 71, 7
SPC_BITS 72, and, 247, 73
SPC_READ 74, cmp, dpx, spc_a
SPC_READ 75, cmp, absx, spc_a
SPC_READ 76, cmp, absy, spc_a
SPC_READ 77, cmp, dpiy, spc_a
SPC_MEMORY_ALU 78, cmp, imm
SPC_MEMORY_ALU 79, cmp, xy
SPC_RMW 7b, ror, dpx
SPC_REG_RMW 7c, ror, spc_a
SPC_TRANSFER 7d, spc_x, spc_a
SPC_READ 7e, cmp, dp, spc_y

SPC_FLAG 80, or, SPC_C, 0
SPC_TABLE_CALL 81, 8
SPC_BITS 82, or, 16, 83
SPC_READ 84, adc, dp, spc_a
SPC_READ 85, adc, abs, spc_a
SPC_READ 86, adc, indx, spc_a
SPC_READ 87, adc, dpix, spc_a
SPC_READ 88, adc, imm, spc_a
SPC_MEMORY_ALU 89, adc, dp
SPC_RMW 8b, dec, dp
SPC_RMW 8c, dec, abs
SPC_READ 8d, mov, imm, spc_y
SPC_PULL 8e, spc_p

SPC_BRANCH 90, SPC_C, jz
SPC_TABLE_CALL 91, 9
SPC_BITS 92, and, 239, 93
SPC_READ 94, adc, dpx, spc_a
SPC_READ 95, adc, absx, spc_a
SPC_READ 96, adc, absy, spc_a
SPC_READ 97, adc, dpiy, spc_a
SPC_MEMORY_ALU 98, adc, imm
SPC_MEMORY_ALU 99, adc, xy
SPC_RMW 9b, dec, dpx
SPC_REG_RMW 9c, dec, spc_a
SPC_TRANSFER 9d, spc_s, spc_x

SPC_FLAG a0, or, SPC_I, 1
SPC_TABLE_CALL a1, 10
SPC_BITS a2, or, 32, a3
SPC_READ a4, sbc, dp, spc_a
SPC_READ a5, sbc, abs, spc_a
SPC_READ a6, sbc, indx, spc_a
SPC_READ a7, sbc, dpix, spc_a
SPC_READ a8, sbc, imm, spc_a
SPC_MEMORY_ALU a9, sbc, dp
SPC_RMW ab, inc, dp
SPC_RMW ac, inc, abs
SPC_READ ad, cmp, imm, spc_y
SPC_PULL ae, spc_a

SPC_BRANCH b0, SPC_C, jnz
SPC_TABLE_CALL b1, 11
SPC_BITS b2, and, 223, b3
SPC_READ b4, sbc, dpx, spc_a
SPC_READ b5, sbc, absx, spc_a
SPC_READ b6, sbc, absy, spc_a
SPC_READ b7, sbc, dpiy, spc_a
SPC_MEMORY_ALU b8, sbc, imm
SPC_MEMORY_ALU b9, sbc, xy
SPC_RMW bb, inc, dpx
SPC_REG_RMW bc, inc, spc_a
SPC_TRANSFER bd, spc_x, spc_s

SPC_FLAG c0, and, ~SPC_I, 1
SPC_TABLE_CALL c1, 12
SPC_BITS c2, or, 64, c3
SPC_STORE c4, dp, spc_a
SPC_STORE c5, abs, spc_a
SPC_STORE c6, indx, spc_a
SPC_STORE c7, dpix, spc_a
SPC_READ c8, cmp, imm, spc_x
SPC_STORE c9, abs, spc_x
SPC_STORE cb, dp, spc_y
SPC_STORE cc, abs, spc_y
SPC_READ cd, mov, imm, spc_x
SPC_PULL ce, spc_x

SPC_BRANCH d0, SPC_Z, jz
SPC_TABLE_CALL d1, 13
SPC_BITS d2, and, 191, d3
SPC_STORE d4, dpx, spc_a
SPC_STORE d5, absx, spc_a
SPC_STORE d6, absy, spc_a
SPC_STORE d8, dp, spc_x
SPC_STORE d9, dpy, spc_x
SPC_STORE db, dpx, spc_y
SPC_REG_RMW dc, dec, spc_y
SPC_TRANSFER dd, spc_y, spc_a

SPC_FLAG e0, and, ~(SPC_V | SPC_H), 0
SPC_TABLE_CALL e1, 14
SPC_BITS e2, or, 128, e3
SPC_READ e4, mov, dp, spc_a
SPC_READ e5, mov, abs, spc_a
SPC_READ e6, mov, indx, spc_a
SPC_READ e7, mov, dpix, spc_a
SPC_READ e8, mov, imm, spc_a
SPC_READ e9, mov, abs, spc_x
SPC_READ eb, mov, dp, spc_y
SPC_READ ec, mov, abs, spc_y
SPC_FLAG ed, xor, SPC_C, 1
SPC_PULL ee, spc_y

SPC_BRANCH f0, SPC_Z, jnz
SPC_TABLE_CALL f1, 15
SPC_BITS f2, and, 127, f3
SPC_READ f4, mov, dpx, spc_a
SPC_READ f5, mov, absx, spc_a
SPC_READ f6, mov, absy, spc_a
SPC_READ f7, mov, dpiy, spc_a
SPC_READ f8, mov, dp, spc_x
SPC_READ f9, mov, dpy, spc_x
SPC_READ fb, mov, dpx, spc_y
SPC_REG_RMW fc, inc, spc_y
SPC_TRANSFER fd, spc_a, spc_y

align 8
spc_dispatch:
%if __BITS__ = 64
    %define SPC_ENTRY dq
%else
    %define SPC_ENTRY dd
%endif
    SPC_ENTRY spc_op_00, spc_op_01, spc_op_02, spc_op_03, spc_op_04, spc_op_05, spc_op_06, spc_op_07
    SPC_ENTRY spc_op_08, spc_op_09, spc_op_0a, spc_op_0b, spc_op_0c, spc_op_0d, spc_op_0e, spc_op_0f
    SPC_ENTRY spc_op_10, spc_op_11, spc_op_12, spc_op_13, spc_op_14, spc_op_15, spc_op_16, spc_op_17
    SPC_ENTRY spc_op_18, spc_op_19, spc_op_1a, spc_op_1b, spc_op_1c, spc_op_1d, spc_op_1e, spc_op_1f
    SPC_ENTRY spc_op_20, spc_op_21, spc_op_22, spc_op_23, spc_op_24, spc_op_25, spc_op_26, spc_op_27
    SPC_ENTRY spc_op_28, spc_op_29, spc_op_2a, spc_op_2b, spc_op_2c, spc_op_2d, spc_op_2e, spc_op_2f
    SPC_ENTRY spc_op_30, spc_op_31, spc_op_32, spc_op_33, spc_op_34, spc_op_35, spc_op_36, spc_op_37
    SPC_ENTRY spc_op_38, spc_op_39, spc_op_3a, spc_op_3b, spc_op_3c, spc_op_3d, spc_op_3e, spc_op_3f
    SPC_ENTRY spc_op_40, spc_op_41, spc_op_42, spc_op_43, spc_op_44, spc_op_45, spc_op_46, spc_op_47
    SPC_ENTRY spc_op_48, spc_op_49, spc_op_4a, spc_op_4b, spc_op_4c, spc_op_4d, spc_op_4e, spc_op_4f
    SPC_ENTRY spc_op_50, spc_op_51, spc_op_52, spc_op_53, spc_op_54, spc_op_55, spc_op_56, spc_op_57
    SPC_ENTRY spc_op_58, spc_op_59, spc_op_5a, spc_op_5b, spc_op_5c, spc_op_5d, spc_op_5e, spc_op_5f
    SPC_ENTRY spc_op_60, spc_op_61, spc_op_62, spc_op_63, spc_op_64, spc_op_65, spc_op_66, spc_op_67
    SPC_ENTRY spc_op_68, spc_op_69, spc_op_6a, spc_op_6b, spc_op_6c, spc_op_6d, spc_op_6e, spc_op_6f
    SPC_ENTRY spc_op_70, spc_op_71, spc_op_72, spc_op_73, spc_op_74, spc_op_75, spc_op_76, spc_op_77
    SPC_ENTRY spc_op_78, spc_op_79, spc_op_7a, spc_op_7b, spc_op_7c, spc_op_7d, spc_op_7e, spc_op_7f
    SPC_ENTRY spc_op_80, spc_op_81, spc_op_82, spc_op_83, spc_op_84, spc_op_85, spc_op_86, spc_op_87
    SPC_ENTRY spc_op_88, spc_op_89, spc_op_8a, spc_op_8b, spc_op_8c, spc_op_8d, spc_op_8e, spc_op_8f
    SPC_ENTRY spc_op_90, spc_op_91, spc_op_92, spc_op_93, spc_op_94, spc_op_95, spc_op_96, spc_op_97
    SPC_ENTRY spc_op_98, spc_op_99, spc_op_9a, spc_op_9b, spc_op_9c, spc_op_9d, spc_op_9e, spc_op_9f
    SPC_ENTRY spc_op_a0, spc_op_a1, spc_op_a2, spc_op_a3, spc_op_a4, spc_op_a5, spc_op_a6, spc_op_a7
    SPC_ENTRY spc_op_a8, spc_op_a9, spc_op_aa, spc_op_ab, spc_op_ac, spc_op_ad, spc_op_ae, spc_op_af
    SPC_ENTRY spc_op_b0, spc_op_b1, spc_op_b2, spc_op_b3, spc_op_b4, spc_op_b5, spc_op_b6, spc_op_b7
    SPC_ENTRY spc_op_b8, spc_op_b9, spc_op_ba, spc_op_bb, spc_op_bc, spc_op_bd, spc_op_be, spc_op_bf
    SPC_ENTRY spc_op_c0, spc_op_c1, spc_op_c2, spc_op_c3, spc_op_c4, spc_op_c5, spc_op_c6, spc_op_c7
    SPC_ENTRY spc_op_c8, spc_op_c9, spc_op_ca, spc_op_cb, spc_op_cc, spc_op_cd, spc_op_ce, spc_op_cf
    SPC_ENTRY spc_op_d0, spc_op_d1, spc_op_d2, spc_op_d3, spc_op_d4, spc_op_d5, spc_op_d6, spc_op_d7
    SPC_ENTRY spc_op_d8, spc_op_d9, spc_op_da, spc_op_db, spc_op_dc, spc_op_dd, spc_op_de, spc_op_df
    SPC_ENTRY spc_op_e0, spc_op_e1, spc_op_e2, spc_op_e3, spc_op_e4, spc_op_e5, spc_op_e6, spc_op_e7
    SPC_ENTRY spc_op_e8, spc_op_e9, spc_op_ea, spc_op_eb, spc_op_ec, spc_op_ed, spc_op_ee, spc_op_ef
    SPC_ENTRY spc_op_f0, spc_op_f1, spc_op_f2, spc_op_f3, spc_op_f4, spc_op_f5, spc_op_f6, spc_op_f7
    SPC_ENTRY spc_op_f8, spc_op_f9, spc_op_fa, spc_op_fb, spc_op_fc, spc_op_fd, spc_op_fe, spc_op_ff
%undef SPC_ENTRY
