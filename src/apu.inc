; Optional external 64-byte S-SMP IPL. The SPC700 runs on its own oscillator.
%define SPC_RATE 1024000
%define NTSC_RATE 21477272
%define PAL_RATE  21281370

apu_reset:
%ifdef APU_IPL
    mov byte [apu_control], 0x80
    mov byte [apu_test], 0x0a
    mov byte [apu_dsp + 0x6c], 0xe0
    call spc_cpu_reset
    mov ax, [apu_ipl_data + 62]
    mov [spc_pc], ax
    mov eax, [tick_lo]
    mov [apu_last_tick], eax
    mov dword [apu_clock_rate], NTSC_RATE
    cmp dword [frame_lines], 312
    jne .done
    mov dword [apu_clock_rate], PAL_RATE
.done:
%endif
    ret

; Preserve registers for bus handlers and synchronize before each port access.
apu_sync:
%ifdef APU_IPL
    cmp byte [apu_halted], 0
    jne .disabled
    PUSH32 eax
    PUSH32 ebx
    PUSH32 ecx
    PUSH32 edx
    PUSH32 ebp
    PUSH32 esi
    PUSH32 edi
    mov eax, [tick_lo]
    mov ecx, eax
    sub eax, [apu_last_tick]
    jz .done
    mov [apu_last_tick], ecx
    mov ecx, SPC_RATE
    mul ecx
    add eax, [apu_remainder]
    adc edx, 0
    div dword [apu_clock_rate]
    mov [apu_remainder], edx
    add [apu_budget], eax
    cmp dword [apu_budget], 0
    jle .done
.run:
    call spc_step
    test eax, eax
    jz .halt
    sub [apu_budget], eax
    call apu_timers
    cmp dword [apu_budget], 0
    jg .run
    jmp .done
.halt:
    mov byte [apu_halted], 1
.done:
    POP32 edi
    POP32 esi
    POP32 ebp
    POP32 edx
    POP32 ecx
    POP32 ebx
    POP32 eax
.disabled:
%endif
    ret

apu_main_read:
    call apu_sync
    and ecx, 3
    movzx eax, byte [apu_to_cpu + ecx]
    ret

apu_main_write:
    call apu_sync
    and ecx, 3
    mov [cpu_to_apu + ecx], dl
    ret

%ifdef APU_IPL
; Reads preserve every register except EAX. Writes preserve all registers.
spc_read:
    and eax, 0xffff
    cmp eax, 0xffc0
    jae .overlay
    cmp eax, 0x00f0
    jb .ram
    cmp eax, 0x00ff
    jbe .io
.ram:
    test byte [apu_test], 4
    jnz .unavailable
    movzx eax, byte [apu_ram + eax]
    ret
.overlay:
    test byte [apu_control], 0x80
    jz .ram
    movzx eax, byte [apu_ipl_data + eax - 0xffc0]
    ret
.unavailable:
    mov eax, 0x5a
    ret
.io:
    cmp eax, 0xf2
    jb .zero
    je .dsp_index
    cmp eax, 0xf3
    je .dsp_data
    cmp eax, 0xf7
    jbe .port
    cmp eax, 0xf9
    jbe .aux
    cmp eax, 0xfd
    jb .zero
    PUSH32 ecx
    mov ecx, eax
    movzx eax, byte [timer_output + ecx - 0xfd]
    mov byte [timer_output + ecx - 0xfd], 0
    POP32 ecx
    ret
.dsp_index:
    movzx eax, byte [apu_dsp_index]
    ret
.dsp_data:
    movzx eax, byte [apu_dsp_index]
    and eax, 0x7f
    movzx eax, byte [apu_dsp + eax]
    ret
.port:
    movzx eax, byte [cpu_to_apu + eax - 0xf4]
    ret
.aux:
    movzx eax, byte [apu_aux + eax - 0xf8]
    ret
.zero:
    xor eax, eax
    ret

spc_write:
    PUSH32 eax
    and eax, 0xffff
    test byte [apu_test], 2
    jz .io
    test byte [apu_test], 4
    jnz .io
    mov [apu_ram + eax], dl
.io:
    cmp eax, 0xf0
    jb .done
    je .test_register
    cmp eax, 0xf1
    je .control
    cmp eax, 0xf2
    je .dsp_index
    cmp eax, 0xf3
    je .dsp_data
    cmp eax, 0xf7
    jbe .port
    cmp eax, 0xf9
    jbe .aux
    cmp eax, 0xfc
    ja .done
    mov [timer_target + eax - 0xfa], dl
.done:
    POP32 eax
    ret
.test_register:
    test byte [spc_p], 0x20
    jnz .done
    mov [apu_test], dl
    jmp .done
.control:
    mov al, [apu_control]
    not al
    and al, dl
    test al, 1
    jz .timer_one
    mov byte [timer_stage], 0
    mov byte [timer_output], 0
.timer_one:
    test al, 2
    jz .timer_two
    mov byte [timer_stage + 1], 0
    mov byte [timer_output + 1], 0
.timer_two:
    test al, 4
    jz .ports
    mov byte [timer_stage + 2], 0
    mov byte [timer_output + 2], 0
.ports:
    test dl, 0x10
    jz .upper_ports
    mov word [cpu_to_apu], 0
.upper_ports:
    test dl, 0x20
    jz .control_store
    mov word [cpu_to_apu + 2], 0
.control_store:
    mov [apu_control], dl
    jmp .done
.dsp_index:
    mov [apu_dsp_index], dl
    jmp .done
.dsp_data:
    movzx eax, byte [apu_dsp_index]
    cmp eax, 0x80
    jae .done
    cmp eax, 0x7c
    jne .dsp_store
    mov byte [apu_dsp + eax], 0
    jmp .done
.dsp_store:
    mov [apu_dsp + eax], dl
    jmp .done
.port:
    mov [apu_to_cpu + eax - 0xf4], dl
    jmp .done
.aux:
    mov [apu_aux + eax - 0xf8], dl
    jmp .done

; Timer prescalers free-run with a 128-cycle or 16-cycle period.
apu_timers:
    xor ecx, ecx
.next:
    movzx edx, byte [timer_phase + ecx]
    add edx, eax
    cmp edx, [timer_divider + ecx * 4]
    jb .phase
    sub edx, [timer_divider + ecx * 4]
    test byte [apu_test], 8
    jz .phase
    test byte [apu_test], 1
    jnz .phase
    bt dword [apu_control_dword], ecx
    jnc .phase
    inc byte [timer_stage + ecx]
    mov bl, [timer_stage + ecx]
    cmp bl, [timer_target + ecx]
    jne .phase
    mov byte [timer_stage + ecx], 0
    inc byte [timer_output + ecx]
    and byte [timer_output + ecx], 15
.phase:
    mov [timer_phase + ecx], dl
    inc ecx
    cmp ecx, 3
    jb .next
    ret

timer_divider: dd 128,128,16

apu_ipl_data:
    incbin APU_IPL
apu_ipl_data_end:
    times -(apu_ipl_data_end - apu_ipl_data != 64) db 0

%include "spc_cpu.inc"
%endif

%macro APU_STATE 0
    cpu_to_apu:     resb 4
    apu_to_cpu:     resb 4
%ifdef APU_IPL
    alignb 4
    apu_last_tick:  resd 1
    apu_clock_rate: resd 1
    apu_budget:     resd 1
    apu_remainder:  resd 1
apu_control_dword:
    apu_control:    resb 1
    apu_test:       resb 1
    apu_halted:     resb 1
    apu_dsp_index:  resb 1
    apu_aux:        resb 2
    timer_phase:    resb 3
    timer_stage:    resb 3
    timer_target:   resb 3
    timer_output:   resb 3
    apu_dsp:        resb 128
    apu_ram:        resb 65536
    SPC_CPU_STATE
%endif
%endmacro
