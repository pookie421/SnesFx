; SNES PPU I/O and native-resolution line renderer. The caller schedules one
; line at a time. Bus phase effects and mid-line register writes need a later
; fetch pipeline.

%macro PPU_STATE 0
ppu_regs:           resb 64
ppu_vram_addr:      resw 1
ppu_vram_buffer:    resw 1
ppu_cgram_addr:     resw 1
ppu_cgram_latch:    resb 1
ppu_oam_addr:       resw 1
ppu_oam_reload:     resw 1
ppu_oam_latch:      resb 1
ppu_scroll:         resw 8
ppu_scroll_latch:   resb 1
ppu_hscroll_latch:  resb 1
ppu_mode7_latch:    resb 1
ppu_mode7_a:        resw 1
ppu_mode7_b:        resw 1
ppu_mode7_c:        resw 1
ppu_mode7_d:        resw 1
ppu_mode7_x:        resw 1
ppu_mode7_y:        resw 1
ppu_mode7_hofs:     resw 1
ppu_mode7_vofs:     resw 1
ppu_bus1:           resb 1
ppu_bus2:           resb 1
ppu_latched_h:      resw 1
ppu_latched_v:      resw 1
ppu_read_h_high:    resb 1
ppu_read_v_high:    resb 1
ppu_counters_valid: resb 1
ppu_obj_status:     resb 1
ppu_window_dirty:   resb 1
ppu_fixed_color:    resw 1
ppu_vram:           resb 65536
ppu_cgram:          resb 512
ppu_oam:            resb 544
video_pixels:       resw 256 * 224
video_frame:        resd 1
; Temporary line-compositor state.
ppu_y:              resd 1
ppu_dest:           resd 1
ppu_main_dest:      resd 1
ppu_screen_layers:  resd 1
ppu_screen_windows: resd 1
ppu_layer_window:   resb 1
ppu_layer_math:     resb 1
ppu_draw_sprites:   resb 1
ppu_allow_color:    resd 1
ppu_allow_math:     resd 1
ppu_mode:           resd 1
ppu_layer:          resd 1
ppu_layer_info:     resd 1
ppu_screen_base:    resd 1
ppu_tile_base:      resd 1
ppu_map_row:        resd 1
ppu_y_delta:        resd 1
ppu_x:              resd 1
ppu_map_x:          resd 1
ppu_color_bank:     resd 1
ppu_high_pixels:    resd 1
ppu_low_pixels:     resd 1
ppu_palette_group:  resd 1
ppu_tile_flags:     resd 1
ppu_tile_stride:    resd 1
ppu_priority:       resd 1
ppu_run:            resd 1
ppu_sprite_number:  resd 1
ppu_sprite_count:   resd 1
ppu_sprite_first:   resd 1
ppu_sprite_size:    resd 1
ppu_sprite_max_h:   resd 1
ppu_sprite_hidden:  resb 1
ppu_sprite_row:     resd 1
ppu_sprite_tiles:   resd 1
ppu_sprite_x:       resd 1
ppu_sprite_width:   resd 1
ppu_sprite_attr:    resd 1
ppu_sprite_tile:    resd 1
ppu_sprite_step:    resd 1
ppu_sprite_data:    resd 1
ppu_z:              resb 256
ppu_window_rule:    resd 1
ppu_window_mask:    resb 256
ppu_line_math:      resb 256
ppu_sub_z:          resb 256
ppu_sub_colors:     resw 256
ppu_obj_z:          resb 256
ppu_obj_math:       resb 256
ppu_obj_colors:     resw 256
ppu_sprite_queue:   resd 32 * 4
ppu_m7_center_x:    resd 1
ppu_m7_center_y:    resd 1
ppu_m7_clip_x:      resd 1
ppu_m7_clip_y:      resd 1
ppu_m7_row:         resd 1
ppu_m7_step_x:      resd 1
ppu_m7_step_y:      resd 1
ppu_m7_samples:     resb 256
%endmacro

ppu_reset:
    mov byte [ppu_regs], 0x80
    ret

; Return the VRAM byte offset of the address after VMAIN remapping.
ppu_vram_offset:
    movzx eax, word [ppu_vram_addr]
    movzx ecx, byte [ppu_regs + 0x15]
    shr ecx, 2
    and ecx, 3
    jz .plain
    mov ebx, eax
    and ebx, [ppu_remap_low + ecx * 4]
    lea ebx, [ebx * 8]
    mov edx, eax
    and edx, [ppu_remap_high + ecx * 4]
    add ecx, 4
    shr edx, cl
    sub ecx, 4
    and eax, [ppu_remap_keep + ecx * 4]
    or eax, ebx
    or eax, edx
.plain:
    and eax, 0x7fff
    add eax, eax
    ret

ppu_step_vram:
    movzx ecx, byte [ppu_regs + 0x15]
    and ecx, 3
    movzx ecx, byte [ppu_vram_steps + ecx]
    add [ppu_vram_addr], cx
    ret

; Port access requires forced blank or vblank. Address counters continue
; advancing even when a write cannot reach VRAM.
ppu_vram_allowed:
    test byte [ppu_regs], 0x80
    jnz .yes
    mov ecx, 225
    test byte [ppu_regs + 0x33], 4
    jz .height
    mov ecx, 240
.height:
    cmp [vcounter], ecx
    jb .no
.yes:
    clc
    ret
.no:
    stc
    ret

ppu_prefetch_vram:
    call ppu_vram_allowed
    jc .blocked
    call ppu_vram_offset
    mov ax, [ppu_vram + eax]
    mov [ppu_vram_buffer], ax
    ret
.blocked:
    mov word [ppu_vram_buffer], 0
    ret

; 0x200–0x3ff mirrors the 32-byte sprite X/size table.
ppu_oam_offset:
    movzx eax, word [ppu_oam_addr]
    and eax, 0x3ff
    cmp eax, 0x200
    jb .low
    and eax, 0x1f
    or eax, 0x200
.low:
    ret

ppu_latch_counters:
    mov eax, [hcounter]
    cmp dword [line_clocks], 1360
    je .dots
    xor ecx, ecx
    xor edx, edx
    cmp eax, 1292
    seta cl
    cmp eax, 1310
    seta dl
    add ecx, edx
    add ecx, ecx
    sub eax, ecx
.dots:
    shr eax, 2
    mov [ppu_latched_h], ax
    mov eax, [vcounter]
    mov [ppu_latched_v], ax
    mov byte [ppu_counters_valid], 1
    ret

; EAX is a full $21xx address. Return -1 for CPU open bus, otherwise a byte.
ppu_read:
    cmp eax, 0x2134
    jb .write_only
    cmp eax, 0x213f
    ja .open
    sub eax, 0x2134
    %if __BITS__ = 64
    jmp [ppu_read_ports + eax * 8]
%else
    jmp [ppu_read_ports + eax * 4]
%endif
.write_only:
    sub eax, 0x2100
    cmp eax, 0x33
    ja .open
    bt dword [ppu_read_bus1], eax
    jnc .open
    movzx eax, byte [ppu_bus1]
    ret
.open:
    mov eax, -1
    ret

ppu_read_mul_low:
    xor ecx, ecx
    jmp ppu_read_mul
ppu_read_mul_mid:
    mov ecx, 8
    jmp ppu_read_mul
ppu_read_mul_high:
    mov ecx, 16
ppu_read_mul:
    movsx eax, word [ppu_mode7_a]
    movsx edx, byte [ppu_mode7_b + 1]
    imul eax, edx
    shr eax, cl
    movzx eax, al
    mov [ppu_bus1], al
    ret

ppu_read_latch:
    test byte [io_port], 0x80
    jz .open
    call ppu_latch_counters
.open:
    mov eax, -1
    ret

ppu_read_oam:
    call ppu_oam_offset
    inc word [ppu_oam_addr]
    movzx eax, byte [ppu_oam + eax]
    mov [ppu_bus1], al
    ret

ppu_read_vram_low:
    xor eax, eax
    jmp ppu_read_vram
ppu_read_vram_high:
    mov eax, 1
ppu_read_vram:
    movzx edi, byte [ppu_vram_buffer + eax]
    movzx ecx, byte [ppu_regs + 0x15]
    shr ecx, 7
    cmp eax, ecx
    jne .done
    call ppu_prefetch_vram
    call ppu_step_vram
.done:
    mov eax, edi
    mov [ppu_bus1], al
    ret

ppu_read_cgram:
    movzx edx, word [ppu_cgram_addr]
    and edx, 0x1ff
    movzx eax, byte [ppu_cgram + edx]
    test edx, 1
    jz .done
    mov dl, [ppu_bus2]
    and edx, 0x80
    or eax, edx
.done:
    inc word [ppu_cgram_addr]
    mov [ppu_bus2], al
    ret

ppu_read_h:
    lea edx, [ppu_latched_h]
    lea ecx, [ppu_read_h_high]
    jmp ppu_read_counter_value
ppu_read_v:
    lea edx, [ppu_latched_v]
    lea ecx, [ppu_read_v_high]
ppu_read_counter_value:
    movzx eax, byte [ecx]
    xor byte [ecx], 1
    movzx eax, byte [edx + eax]
    cmp byte [ecx], 0
    jne .done
    and eax, 1
    movzx edx, byte [ppu_bus2]
    and edx, 0xfe
    or eax, edx
.done:
    mov [ppu_bus2], al
    ret

ppu_read_status1:
    movzx eax, byte [ppu_bus1]
    and eax, 0x10
    or al, [ppu_obj_status]
    or eax, 1
    mov [ppu_bus1], al
    ret

ppu_read_status2:
    movzx eax, byte [ppu_bus2]
    and eax, 0x20
    movzx edx, byte [field_odd]
    shl edx, 7
    or eax, edx
    cmp dword [frame_lines], 312
    jne .latch_status
    or eax, 0x10
.latch_status:
    test byte [io_port], 0x80
    jz .latch_low
    cmp byte [ppu_counters_valid], 0
    je .done
    mov byte [ppu_counters_valid], 0
.latch_low:
    or eax, 0x40
.done:
    or eax, 3
    mov byte [ppu_read_h_high], 0
    mov byte [ppu_read_v_high], 0
    mov [ppu_bus2], al
    ret

; EAX = $21xx, DL = value.
ppu_write:
    sub eax, 0x2100
    cmp eax, 0x33
    ja .done
    mov [ppu_regs + eax], dl
    cmp eax, 2
    je .oam_low
    cmp eax, 3
    je .oam_high
    cmp eax, 4
    je .oam_data
    cmp eax, 0x0d
    jb .done
    cmp eax, 0x14
    jbe .scroll
    cmp eax, 0x16
    je .vram_addr_low
    cmp eax, 0x17
    je .vram_addr_high
    cmp eax, 0x18
    je .vram_low
    cmp eax, 0x19
    je .vram_high
    cmp eax, 0x1b
    jb .done
    cmp eax, 0x20
    jbe .mode7_matrix
    cmp eax, 0x21
    je .cgram_addr
    cmp eax, 0x22
    je .cgram_data
    cmp eax, 0x2b
    jbe .window
    cmp eax, 0x32
    je .fixed_color
.done:
    ret
.oam_low:
    mov [ppu_oam_reload], dl
    jmp .oam_reset
.oam_high:
    and edx, 1
    mov [ppu_oam_reload + 1], dl
.oam_reset:
    mov ax, [ppu_oam_reload]
    add ax, ax
    mov [ppu_oam_addr], ax
    ret
.oam_data:
    call ppu_oam_offset
    inc word [ppu_oam_addr]
    cmp eax, 0x200
    jae .oam_direct
    test eax, 1
    jnz .oam_pair
    mov [ppu_oam_latch], dl
    ret
.oam_pair:
    mov cl, [ppu_oam_latch]
    mov [ppu_oam + eax - 1], cl
.oam_direct:
    mov [ppu_oam + eax], dl
    ret
.scroll:
    sub eax, 0x0d
    and edx, 0xff
    shl edx, 8
    movzx ecx, byte [ppu_scroll_latch]
    test eax, 1
    jnz .scroll_v
    and ecx, 0xf8
    movzx ebx, byte [ppu_hscroll_latch]
    and ebx, 7
    or ecx, ebx
    mov [ppu_hscroll_latch], dh
.scroll_v:
    or ecx, edx
    mov [ppu_scroll + eax * 2], cx
    mov [ppu_scroll_latch], dh
    cmp eax, 2
    jae .done
    movzx ecx, byte [ppu_mode7_latch]
    or ecx, edx
    mov [ppu_mode7_hofs + eax * 2], cx
    mov [ppu_mode7_latch], dh
    ret
.vram_addr_low:
    mov [ppu_vram_addr], dl
    jmp ppu_prefetch_vram
.vram_addr_high:
    mov [ppu_vram_addr + 1], dl
    jmp ppu_prefetch_vram
.vram_low:
    xor esi, esi
    jmp .vram_data
.vram_high:
    mov esi, 1
.vram_data:
    mov edi, edx
    call ppu_vram_allowed
    jc .vram_advance
    call ppu_vram_offset
    add eax, esi
    mov edx, edi
    mov [ppu_vram + eax], dl
.vram_advance:
    movzx ecx, byte [ppu_regs + 0x15]
    shr ecx, 7
    cmp ecx, esi
    jne .done
    jmp ppu_step_vram
.mode7_matrix:
    sub eax, 0x1b
    mov cl, [ppu_mode7_latch]
    mov [ppu_mode7_a + eax * 2], cl
    mov [ppu_mode7_a + eax * 2 + 1], dl
    mov [ppu_mode7_latch], dl
    ret
.cgram_addr:
    movzx eax, dl
    add eax, eax
    mov [ppu_cgram_addr], ax
    ret
.cgram_data:
    movzx eax, word [ppu_cgram_addr]
    and eax, 0x1ff
    inc word [ppu_cgram_addr]
    test eax, 1
    jnz .cgram_pair
    mov [ppu_cgram_latch], dl
    ret
.cgram_pair:
    mov cl, [ppu_cgram_latch]
    and edx, 0x7f
    mov [ppu_cgram + eax - 1], cl
    mov [ppu_cgram + eax], dl
    ret
.window:
    mov byte [ppu_window_dirty], 1
    ret
.fixed_color:
    movzx eax, word [ppu_fixed_color]
    mov ecx, edx
    and ecx, 31
    test dl, 0x20
    jz .fixed_green
    and eax, 0x7fe0
    or eax, ecx
.fixed_green:
    shl ecx, 5
    test dl, 0x40
    jz .fixed_blue
    and eax, 0x7c1f
    or eax, ecx
.fixed_blue:
    shl ecx, 5
    test dl, 0x80
    jz .fixed_store
    and eax, 0x03ff
    or eax, ecx
.fixed_store:
    mov [ppu_fixed_color], ax
    ret

; Absolute jump table entries use the current host's near pointer size.
%if __BITS__ = 64
    %define PPU_PTR dq
%else
    %define PPU_PTR dd
%endif
ppu_read_ports:
    PPU_PTR ppu_read_mul_low, ppu_read_mul_mid, ppu_read_mul_high, ppu_read_latch
    PPU_PTR ppu_read_oam, ppu_read_vram_low, ppu_read_vram_high, ppu_read_cgram
    PPU_PTR ppu_read_h, ppu_read_v, ppu_read_status1, ppu_read_status2
%undef PPU_PTR
ppu_read_bus1: dq 0x0000077007700770
ppu_vram_steps: db 1, 32, 128, 128
align 4
ppu_remap_low:  dd 0, 0x1f, 0x3f, 0x7f
ppu_remap_high: dd 0, 0xe0, 0x1c0, 0x380
ppu_remap_keep: dd 0, 0x7f00, 0x7e00, 0x7c00

; EAX is the PPU line (1-224). Store host-endian BGR555 in video_pixels.
ppu_scanline:
    cmp eax, 1
    jb .done
    cmp eax, 224
    ja .done
    PUSH32 ebx
    PUSH32 esi
    PUSH32 edi
    PUSH32 ebp

    mov [ppu_y], eax
    dec eax
    shl eax, 9
    add eax, video_pixels
    mov [ppu_main_dest], eax
    cmp dword [ppu_y], 1
    jne .check_blank
    mov byte [ppu_obj_status], 0
.check_blank:
    cld
    test byte [ppu_regs], 0x80
    jz .compose
    mov edi, eax
    xor eax, eax
    mov ecx, 256
    rep stosw
    jmp .restore
.compose:
    movzx eax, byte [ppu_regs + 5]
    and eax, 7
    mov [ppu_mode], eax
    cmp byte [ppu_window_dirty], 0
    je .windows_ready
    call ppu_update_windows
.windows_ready:
    mov al, [ppu_regs + 0x2c]
    or al, [ppu_regs + 0x2d]
    mov [ppu_draw_sprites], al
    cmp dword [ppu_mode], 7
    jne .sprites
    test al, 3
    jz .sprites
    call ppu_sample_mode7
.sprites:
    ; Evaluate OBJ overflow even when the OBJ layer is hidden.
    call ppu_render_obj

    test byte [ppu_regs + 0x30], 2
    jz .main
    test byte [ppu_regs + 0x31], 0x3f
    jz .main
    ; Skip the sub-screen pass if all its sources are disabled.
    test byte [ppu_regs + 0x2d], 0x1f
    jnz .draw_sub
    xor eax, eax
    lea edi, [ppu_sub_z]
    mov ecx, 64
    rep stosd
    jmp .main
.draw_sub:
    mov dword [ppu_dest], ppu_sub_colors
    movzx eax, byte [ppu_regs + 0x2d]
    mov [ppu_screen_layers], eax
    movzx eax, byte [ppu_regs + 0x2f]
    mov [ppu_screen_windows], eax
    call ppu_draw_plane
    lea esi, [ppu_z]
    lea edi, [ppu_sub_z]
    mov ecx, 64
    rep movsd
.main:
    mov eax, [ppu_main_dest]
    mov [ppu_dest], eax
    movzx eax, byte [ppu_regs + 0x2c]
    mov [ppu_screen_layers], eax
    movzx eax, byte [ppu_regs + 0x2e]
    mov [ppu_screen_windows], eax
    call ppu_draw_plane
    call ppu_apply_color_math
    call ppu_apply_brightness
.restore:
    POP32 ebp
    POP32 edi
    POP32 esi
    POP32 ebx
.done:
    ret

ppu_draw_plane:
    mov edi, [ppu_dest]
    mov ax, [ppu_cgram]
    mov ecx, 256
    rep stosw
    xor eax, eax
    lea edi, [ppu_z]
    mov ecx, 64
    rep stosd
    mov al, [ppu_regs + 0x31]
    and al, 0x20
    lea edi, [ppu_line_math]
    mov ecx, 256
    rep stosb

    mov dword [ppu_layer], 0
.next_bg:
    mov ecx, [ppu_layer]
    mov edx, 1
    shl edx, cl
    test [ppu_screen_layers], edx
    jz .step_bg
    mov eax, [ppu_screen_windows]
    and eax, edx
    mov [ppu_layer_window], al
    and dl, [ppu_regs + 0x31]
    mov [ppu_layer_math], dl
    mov eax, [ppu_mode]
    cmp eax, 7
    je .mode7
    lea eax, [ecx + eax * 4]
    mov eax, [ppu_bg_modes + eax * 4]
    test eax, eax
    jz .step_bg
    mov [ppu_layer_info], eax
    call ppu_render_bg
    jmp .step_bg
.mode7:
    cmp ecx, 1
    ja .step_bg
    jb .draw_mode7
    test byte [ppu_regs + 0x33], 0x40
    jz .step_bg
.draw_mode7:
    call ppu_render_mode7
.step_bg:
    inc dword [ppu_layer]
    cmp dword [ppu_layer], 4
    jb .next_bg
    jmp ppu_merge_obj

; Rank bytes match each mode's interleaving of BG and OBJ priorities.
; Columns per layer entry are [bpp, low rank, high rank, unused].
ppu_bg_modes:
    dd 0x0b0802, 0x0a0702, 0x050202, 0x040102 ; 0
    dd 0x090604, 0x080504, 0x030102, 0        ; 1
    dd 0, 0, 0, 0                           ; 2
    dd 0x070308, 0x050104, 0, 0              ; 3
    dd 0, 0, 0, 0                           ; 4
    dd 0, 0, 0, 0                           ; 5
    dd 0, 0, 0, 0                           ; 6
    dd 0, 0, 0, 0                           ; 7
ppu_obj_priorities:
    db 3, 6, 9, 12
    db 2, 4, 7, 10
    db 2, 4, 6, 8
    db 2, 4, 6, 8
    db 2, 4, 6, 8
    db 2, 4, 6, 8
    db 1, 3, 4, 6
    db 2, 4, 6, 7

ppu_render_bg:
    mov ecx, [ppu_layer]
    movzx ebx, byte [ppu_regs + ecx + 7]
    mov eax, ebx
    and eax, 0xfc
    shl eax, 9
    mov [ppu_screen_base], eax
    mov edx, ecx
    shr edx, 1
    movzx eax, byte [ppu_regs + edx + 0x0b]
    test ecx, 1
    jz .low_base
    shr eax, 4
.low_base:
    and eax, 0x0f
    shl eax, 13
    mov [ppu_tile_base], eax

    movzx eax, word [ppu_scroll + ecx * 4 + 2]
    add eax, [ppu_y]
    mov edx, eax
    add ecx, 4
    bt dword [ppu_regs + 5], ecx
    setc cl
    movzx ecx, cl
    add ecx, 3
    mov [ppu_tile_stride], ecx
    and edx, 15
    mov [ppu_y_delta], edx
    shr eax, cl
    mov edx, eax
    and edx, 31
    shl edx, 5
    test eax, 32
    jz .map_row
    test ebx, 2
    jz .map_row
    add edx, 1024
    test ebx, 1
    jz .map_row
    add edx, 1024
.map_row:
    mov [ppu_map_row], edx
    mov ecx, [ppu_layer]
    movzx eax, word [ppu_scroll + ecx * 4]
    mov [ppu_map_x], eax
    mov dword [ppu_x], 0
.next_cell:
    mov eax, [ppu_map_x]
    mov ecx, [ppu_tile_stride]
    shr eax, cl
    mov edx, eax
    and eax, 31
    add eax, [ppu_map_row]
    test edx, 32
    jz .map_address
    mov ecx, [ppu_layer]
    test byte [ppu_regs + ecx + 7], 1
    jz .map_address
    add eax, 1024
.map_address:
    lea eax, [eax * 2]
    add eax, [ppu_screen_base]
    and eax, 0xffff
    movzx edx, word [ppu_vram + eax]
    mov [ppu_tile_flags], edx
    movzx eax, byte [ppu_layer_info + 1]
    test edx, 0x2000
    jz .rank_known
    movzx eax, byte [ppu_layer_info + 2]
    cmp dword [ppu_mode], 1
    jne .rank_known
    cmp dword [ppu_layer], 2
    jne .rank_known
    test byte [ppu_regs + 5], 8
    jz .rank_known
    mov eax, 11
.rank_known:
    mov [ppu_priority], eax
    mov eax, edx
    shr eax, 10
    and eax, 7
    mov [ppu_palette_group], eax
    movzx ecx, byte [ppu_layer_info]
    shl eax, cl
    cmp dword [ppu_mode], 0
    jne .palette_known
    mov ebx, [ppu_layer]
    shl ebx, 5
    add eax, ebx
.palette_known:
    mov [ppu_color_bank], eax

    mov eax, edx
    and eax, 0x3ff
    mov ebx, [ppu_y_delta]
    mov esi, [ppu_map_x]
    test edx, 0x4000
    jz .flip_v
    xor esi, 8
.flip_v:
    test edx, 0x8000
    jz .cell_size
    xor ebx, 15
.cell_size:
    cmp dword [ppu_tile_stride], 4
    jne .tile_index
    test esi, 8
    jz .tile_row
    inc eax
.tile_row:
    test ebx, 8
    jz .tile_index
    add eax, 16
.tile_index:
    and eax, 0x3ff
    and ebx, 7
    shl eax, 4
    cmp ecx, 2
    je .tile_addr
    add eax, eax
    cmp ecx, 4
    je .tile_addr
    add eax, eax
.tile_addr:
    lea eax, [eax + ebx * 2]
    add eax, [ppu_tile_base]
    and eax, 0xffff
    cmp ecx, 8
    je .decode8
    call ppu_decode_row
    jmp .start_span
.decode8:
    mov ecx, 4
    call ppu_decode_row
    mov [ppu_low_pixels], ebx
    add eax, 32
    mov edx, [ppu_tile_flags]
    call ppu_decode_row
    mov [ppu_high_pixels], ebx
    mov ebx, [ppu_low_pixels]
.start_span:
    mov eax, [ppu_map_x]
    and eax, 7
    mov ecx, eax
    shl ecx, 2
    shr ebx, cl
    mov ecx, 8
    sub ecx, eax
    mov esi, [ppu_x]
    mov eax, 256
    sub eax, esi
    cmp ecx, eax
    jbe .span_size
    mov ecx, eax
.span_size:
    mov [ppu_run], ecx
    cmp byte [ppu_layer_info], 8
    je .draw8
    test ebx, ebx
    jz .advance_cell
    mov edi, [ppu_dest]
    mov edx, [ppu_priority]
    mov dh, [ppu_layer_window]
    mov ebp, [ppu_color_bank]
.next_pixel:
    mov eax, ebx
    and eax, 15
    jz .skip_pixel
    test [ppu_window_mask + esi], dh
    jnz .skip_pixel
    cmp [ppu_z + esi], dl
    jae .skip_pixel
    mov [ppu_z + esi], dl
    add eax, ebp
    mov ax, [ppu_cgram + eax * 2]
    mov [edi + esi * 2], ax
    mov al, [ppu_layer_math]
    mov [ppu_line_math + esi], al
.skip_pixel:
    inc esi
    shr ebx, 4
    dec ecx
    jnz .next_pixel
    jmp .advance_cell
.draw8:
    call ppu_draw8_span
.advance_cell:
    mov eax, [ppu_run]
    add [ppu_map_x], eax
    add [ppu_x], eax
    cmp dword [ppu_x], 256
    jb .next_cell
    ret

; The upper planes retain a separate packed row on 32-bit hosts.
ppu_draw8_span:
    mov eax, [ppu_map_x]
    and eax, 7
    shl eax, 2
    xchg eax, ecx
    mov ebp, [ppu_high_pixels]
    shr ebp, cl
    mov ecx, eax
    mov eax, ebx
    or eax, ebp
    jz .done
    mov edi, [ppu_dest]
    mov edx, [ppu_priority]
    mov dh, [ppu_layer_window]
.next:
    mov eax, ebp
    and eax, 15
    shl eax, 4
    mov ah, bl
    and ah, 15
    or al, ah
    movzx eax, al
    jz .step
    test [ppu_window_mask + esi], dh
    jnz .step
    cmp [ppu_z + esi], dl
    jae .step
    mov [ppu_z + esi], dl
    test byte [ppu_regs + 0x30], 1
    jz .palette
    PUSH32 edx
    mov edx, [ppu_palette_group]
    shl edx, 9
    mov ax, [ppu_direct_colors + edx + eax * 2]
    POP32 edx
    jmp .plot
.palette:
    mov ax, [ppu_cgram + eax * 2]
.plot:
    mov [edi + esi * 2], ax
    mov al, [ppu_layer_math]
    mov [ppu_line_math + esi], al
.step:
    inc esi
    shr ebp, 4
    shr ebx, 4
    dec ecx
    jnz .next
.done:
    ret

; EAX = VRAM byte address, ECX = 2 or 4 bpp, EDX bit 14 = flip.
; Return the eight 4-bit pixel indices in EBX, leftmost in the low nibble.
ppu_decode_row:
    shr edx, 6
    and edx, 0x100
    movzx ebx, byte [ppu_vram + eax]
    or ebx, edx
    mov ebx, [ppu_plane_bits + ebx * 4]
    movzx esi, byte [ppu_vram + eax + 1]
    or esi, edx
    mov esi, [ppu_plane_bits + esi * 4]
    lea ebx, [ebx + esi * 2]
    test ecx, 4
    jz .done
    movzx esi, byte [ppu_vram + eax + 16]
    or esi, edx
    mov esi, [ppu_plane_bits + esi * 4]
    lea ebx, [ebx + esi * 4]
    movzx esi, byte [ppu_vram + eax + 17]
    or esi, edx
    mov esi, [ppu_plane_bits + esi * 4]
    lea ebx, [ebx + esi * 8]
.done:
    ret

; OBJSEL index, then small/large dimension.
ppu_obj_widths:  db 8,16,8,32,8,64,16,32,16,64,32,64,16,32,16,32
ppu_obj_heights: db 8,16,8,32,8,64,16,32,16,64,32,64,32,64,32,32

ppu_render_obj:
    xor eax, eax
    lea edi, [ppu_obj_z]
    mov ecx, 64
    rep stosd
    mov [ppu_sprite_number], eax
    mov [ppu_sprite_count], eax
    mov [ppu_sprite_tiles], eax
    test byte [ppu_regs + 3], 0x80
    jz .first
    movzx eax, word [ppu_oam_addr]
    shr eax, 2
    and eax, 127
.first:
    mov [ppu_sprite_first], eax
    movzx eax, byte [ppu_regs + 1]
    shr eax, 5
    add eax, eax
    mov [ppu_sprite_size], eax
    movzx eax, byte [ppu_obj_heights + eax + 1]
    mov [ppu_sprite_max_h], eax
.next_sprite:
    mov edi, [ppu_sprite_number]
    add edi, [ppu_sprite_first]
    and edi, 127
    mov ebp, [ppu_y]
    dec ebp
    movzx eax, byte [ppu_oam + edi * 4 + 1]
    sub ebp, eax
    and ebp, 255
    cmp ebp, [ppu_sprite_max_h]
    jae .next_candidate

    mov ecx, edi
    and ecx, 3
    add ecx, ecx
    mov esi, edi
    shr esi, 2
    movzx eax, byte [ppu_oam + 0x200 + esi]
    shr eax, cl
    mov ecx, eax
    and ecx, 1
    shl ecx, 8
    movzx edx, byte [ppu_oam + edi * 4]
    or edx, ecx
    cmp edx, 256
    jb .sprite_x
    sub edx, 512
.sprite_x:
    shr eax, 1
    and eax, 1
    add eax, [ppu_sprite_size]
    movzx ecx, byte [ppu_obj_widths + eax]
    cmp edx, -256
    je .sprite_y_test
    lea ebx, [edx + ecx]
    test ebx, ebx
    jle .next_candidate
.sprite_y_test:
    movzx eax, byte [ppu_obj_heights + eax]
    cmp ebp, eax
    jae .next_candidate
    mov eax, [ppu_sprite_count]
    cmp eax, 32
    jae .range_over
    shl eax, 4
    mov [ppu_sprite_queue + eax], edx
    mov [ppu_sprite_queue + eax + 4], ebp
    mov [ppu_sprite_queue + eax + 8], ecx
    mov [ppu_sprite_queue + eax + 12], edi
    inc dword [ppu_sprite_count]
.next_candidate:
    inc dword [ppu_sprite_number]
    cmp dword [ppu_sprite_number], 128
    jb .next_sprite
    jmp .next_fetch
.range_over:
    or byte [ppu_obj_status], 0x40

    ; Sprites are evaluated forward, then tiles are fetched in reverse
    ; sprite order. Low OAM indices therefore overwrite later sprites.
.next_fetch:
    sub dword [ppu_sprite_count], 1
    jc .done
    mov eax, [ppu_sprite_count]
    shl eax, 4
    mov edx, [ppu_sprite_queue + eax]
    mov [ppu_sprite_x], edx
    cmp edx, -256
    sete byte [ppu_sprite_hidden]
    mov ecx, [ppu_sprite_queue + eax + 4]
    mov ebx, [ppu_sprite_queue + eax + 8]
    mov [ppu_sprite_width], ebx
    mov edi, [ppu_sprite_queue + eax + 12]
    movzx edx, byte [ppu_oam + edi * 4 + 3]
    mov [ppu_sprite_attr], edx
    test dl, 0x80
    jz .sprite_y
    ; Rectangular sprites mirror each square section independently.
    mov eax, [ppu_sprite_width]
    dec eax
    xor ecx, eax
.sprite_y:
    mov eax, ecx
    and ecx, 7
    mov [ppu_sprite_row], ecx
    and eax, 0x38
    add eax, eax
    movzx ecx, byte [ppu_oam + edi * 4 + 2]
    add eax, ecx
    and eax, 0xf0
    mov ebx, ecx
    and ebx, 0x0f
    mov dword [ppu_sprite_step], 1
    test dl, 0x40
    jz .sprite_tile
    mov ecx, [ppu_sprite_width]
    shr ecx, 3
    lea ebx, [ebx + ecx - 1]
    mov dword [ppu_sprite_step], -1
.sprite_tile:
    and ebx, 15
    or eax, ebx
    mov [ppu_sprite_tile], eax
    movzx eax, byte [ppu_regs + 1]
    mov ecx, eax
    and eax, 7
    shl eax, 14
    test dl, 1
    jz .sprite_base
    shr ecx, 3
    and ecx, 3
    inc ecx
    shl ecx, 13
    add eax, ecx
.sprite_base:
    mov [ppu_sprite_data], eax
    mov eax, edx
    shr eax, 1
    and eax, 7
    shl eax, 4
    add eax, 128
    mov [ppu_color_bank], eax
    shr edx, 4
    and edx, 3
    mov eax, [ppu_mode]
    lea edx, [edx + eax * 4]
    movzx eax, byte [ppu_obj_priorities + edx]
    mov [ppu_priority], eax
.next_tile:
    mov esi, [ppu_sprite_x]
    cmp esi, 256
    jge .next_fetch
    cmp esi, -7
    jge .count_tile
    cmp byte [ppu_sprite_hidden], 0
    je .tile_step
.count_tile:
    inc dword [ppu_sprite_tiles]
    cmp dword [ppu_sprite_tiles], 34
    ja .time_over
    cmp esi, -7
    jl .tile_step
    test byte [ppu_draw_sprites], 0x10
    jz .tile_step

    mov eax, [ppu_sprite_tile]
    shl eax, 5
    mov ecx, [ppu_sprite_row]
    lea eax, [eax + ecx * 2]
    add eax, [ppu_sprite_data]
    and eax, 0xffff
    mov edx, [ppu_sprite_attr]
    shl edx, 8
    mov ecx, 4
    call ppu_decode_row
    test ebx, ebx
    jz .tile_step
    mov esi, [ppu_sprite_x]
    mov ecx, 8
    mov edx, [ppu_priority]
    mov ebp, [ppu_color_bank]
    cmp ebp, 192
    setae dh
    test byte [ppu_regs + 0x31], 0x10
    jnz .obj_pixel
    xor dh, dh
.obj_pixel:
    cmp esi, 255
    ja .obj_skip
    mov eax, ebx
    and eax, 15
    jz .obj_skip
    mov [ppu_obj_z + esi], dl
    mov [ppu_obj_math + esi], dh
    add eax, ebp
    mov ax, [ppu_cgram + eax * 2]
    mov [ppu_obj_colors + esi * 2], ax
.obj_skip:
    inc esi
    shr ebx, 4
    dec ecx
    jnz .obj_pixel
.tile_step:
    mov eax, [ppu_sprite_tile]
    mov edx, eax
    add eax, [ppu_sprite_step]
    and eax, 0x0f
    and edx, 0xf0
    or eax, edx
    mov [ppu_sprite_tile], eax
    add dword [ppu_sprite_x], 8
    sub dword [ppu_sprite_width], 8
    jnz .next_tile
    jmp .next_fetch
.time_over:
    or byte [ppu_obj_status], 0x80
.done:
    ret

ppu_merge_obj:
    test byte [ppu_screen_layers], 0x10
    jz .done
    mov edi, [ppu_dest]
    mov dl, [ppu_screen_windows]
    and dl, 0x10
    xor ecx, ecx
.merge_pixel:
    test [ppu_window_mask + ecx], dl
    jnz .merge_step
    mov al, [ppu_obj_z + ecx]
    cmp al, [ppu_z + ecx]
    jbe .merge_step
    mov [ppu_z + ecx], al
    mov ax, [ppu_obj_colors + ecx * 2]
    mov [edi + ecx * 2], ax
    mov al, [ppu_obj_math + ecx]
    mov [ppu_line_math + ecx], al
.merge_step:
    inc ecx
    cmp ecx, 256
    jb .merge_pixel
.done:
    ret

; Build a six-bit per-pixel mask for the two window ranges and six targets.
; The low five bits gate layers; bit five selects the color-math region.
ppu_update_windows:
    mov byte [ppu_window_dirty], 0
    mov dword [ppu_window_rule], 0
    xor esi, esi
    mov ebp, 1
.target:
    mov ecx, esi
    shr ecx, 1
    movzx eax, byte [ppu_regs + ecx + 0x23]
    test esi, 1
    jz .selector
    shr eax, 4
.selector:
    and eax, 15
    movzx edx, word [ppu_regs + 0x2a]
    lea ecx, [esi * 2]
    shr edx, cl
    and edx, 3
    shl edx, 6
    lea edx, [ppu_window_truth + edx + eax * 4]
    mov eax, [edx]
    imul eax, ebp
    or [ppu_window_rule], eax
    shl ebp, 1
    inc esi
    cmp esi, 6
    jb .target

    xor ecx, ecx
    movzx ebx, byte [ppu_regs + 0x26]
    movzx edx, byte [ppu_regs + 0x27]
    movzx edi, byte [ppu_regs + 0x28]
    movzx ebp, byte [ppu_regs + 0x29]
.position:
    xor eax, eax
    cmp ecx, ebx
    jb .second_window
    cmp ecx, edx
    ja .second_window
    inc eax
.second_window:
    cmp ecx, edi
    jb .apply_window
    cmp ecx, ebp
    ja .apply_window
    or eax, 2
.apply_window:
    mov al, [ppu_window_rule + eax]
    mov [ppu_window_mask + ecx], al
    inc ecx
    cmp ecx, 256
    jb .position
    ret

ppu_apply_color_math:
    movzx eax, byte [ppu_regs + 0x30]
    mov ecx, eax
    shr eax, 6
    xor eax, 3
    mov [ppu_allow_color], eax
    shr ecx, 4
    and ecx, 3
    xor ecx, 3
    mov [ppu_allow_math], ecx
    movzx ebp, byte [ppu_regs + 0x31]
    test ebp, 0x3f
    jnz .draw
    cmp eax, 3
    je .done
.draw:
    mov edi, [ppu_dest]
    xor esi, esi
.next:
    movzx ecx, byte [ppu_window_mask + esi]
    shr ecx, 5
    movzx eax, word [edi + esi * 2]
    bt dword [ppu_allow_color], ecx
    sbb ebx, ebx
    and eax, ebx
    cmp byte [ppu_line_math + esi], 0
    je .store
    bt dword [ppu_allow_math], ecx
    jnc .store
    and ebx, ebp ; Half intensity is disabled when main-color clipping is active.
    movzx edx, word [ppu_fixed_color]
    test byte [ppu_regs + 0x30], 2
    jz .operand
    cmp byte [ppu_sub_z + esi], 0
    je .transparent_sub
    movzx edx, word [ppu_sub_colors + esi * 2]
    jmp .operand
.transparent_sub:
    xor ebx, ebx ; Use fixed color at full strength for transparent sub pixels.
.operand:
    mov ecx, eax
    xor ecx, edx
    test ebp, 0x80
    jnz .subtract
    and ecx, 0x0421
    add eax, edx
    test ebx, 0x40
    jz .add_full
    sub eax, ecx
    shr eax, 1
    jmp .store
.add_full:
    mov edx, eax
    sub edx, ecx
    and edx, 0x8420
    sub eax, edx
    mov ecx, edx
    shr ecx, 5
    sub edx, ecx
    or eax, edx
    jmp .store
.subtract:
    and ecx, 0x8420
    sub eax, edx
    add eax, 0x8420
    mov edx, eax
    sub edx, ecx
    and edx, 0x8420
    sub eax, edx
    mov ecx, edx
    shr ecx, 5
    sub edx, ecx
    and eax, edx
    test ebx, 0x40
    jz .store
    and eax, 0x7bde
    shr eax, 1
.store:
    mov [edi + esi * 2], ax
    inc esi
    cmp esi, 256
    jb .next
.done:
    ret

; Mode 7 multiplies signed values, then drops six fractional bits from each
; separate origin term. A/C advance the 24.8 coordinates across the line.
ppu_sample_mode7:
    movsx eax, word [ppu_mode7_x]
    shl eax, 19
    sar eax, 19
    mov [ppu_m7_center_x], eax
    movsx edx, word [ppu_mode7_hofs]
    shl edx, 19
    sar edx, 19
    sub edx, eax
    call ppu_clip_mode7_offset
    mov [ppu_m7_clip_x], edx

    movsx eax, word [ppu_mode7_y]
    shl eax, 19
    sar eax, 19
    mov [ppu_m7_center_y], eax
    movsx edx, word [ppu_mode7_vofs]
    shl edx, 19
    sar edx, 19
    sub edx, eax
    call ppu_clip_mode7_offset
    mov [ppu_m7_clip_y], edx
    mov eax, [ppu_y]
    test byte [ppu_regs + 0x1a], 2
    jz .row
    neg eax
    add eax, 255
.row:
    mov [ppu_m7_row], eax
    mov eax, [ppu_m7_center_x]
    xor ecx, ecx
    call ppu_mode7_origin
    mov ebx, eax
    mov [ppu_m7_step_x], esi
    mov eax, [ppu_m7_center_y]
    mov ecx, 4
    call ppu_mode7_origin
    mov ebp, eax
    mov [ppu_m7_step_y], esi
    xor edi, edi
.next:
    mov eax, ebx
    sar eax, 8
    mov edx, ebp
    sar edx, 8
    mov ecx, eax
    or ecx, edx
    test ecx, 0xfffffc00
    jz .map
    mov cl, [ppu_regs + 0x1a]
    and cl, 0xc0
    cmp cl, 0x80
    je .empty
    cmp cl, 0xc0
    jne .map
    xor ecx, ecx
    jmp .tile
.map:
    mov esi, edx
    and esi, 0x3f8
    shl esi, 4
    mov ecx, eax
    shr ecx, 3
    and ecx, 0x7f
    add esi, ecx
    movzx ecx, byte [ppu_vram + esi * 2]
.tile:
    and eax, 7
    and edx, 7
    lea eax, [eax + edx * 8]
    shl ecx, 6
    add eax, ecx
    mov al, [ppu_vram + eax * 2 + 1]
    jmp .sample
.empty:
    xor eax, eax
.sample:
    mov [ppu_m7_samples + edi], al
    add ebx, [ppu_m7_step_x]
    add ebp, [ppu_m7_step_y]
    inc edi
    cmp edi, 256
    jb .next
    ret

ppu_clip_mode7_offset:
    test edx, 0x2000
    jz .positive
    or edx, -1024
    ret
.positive:
    and edx, 1023
    ret

; EAX = center, ECX = offset of the first matrix coefficient. Return EAX =
; initial coordinate and ESI = horizontal increment.
ppu_mode7_origin:
    shl eax, 8
    movsx esi, word [ppu_mode7_a + ecx]
    movsx edi, word [ppu_mode7_b + ecx]
    mov edx, [ppu_m7_clip_x]
    imul edx, esi
    and edx, -64
    add eax, edx
    mov edx, [ppu_m7_clip_y]
    imul edx, edi
    and edx, -64
    add eax, edx
    imul edi, [ppu_m7_row]
    and edi, -64
    add eax, edi
    test byte [ppu_regs + 0x1a], 1
    jz .done
    imul edx, esi, 255
    add eax, edx
    neg esi
.done:
    ret

; BG1 uses all eight bits. EXTBG exposes bit 7 as BG2 priority and the
; remaining seven bits as its palette index.
ppu_render_mode7:
    mov edi, [ppu_dest]
    xor ecx, ecx
    mov dh, [ppu_layer_window]
.next:
    test [ppu_window_mask + ecx], dh
    jnz .step
    movzx eax, byte [ppu_m7_samples + ecx]
    mov dl, 3
    cmp dword [ppu_layer], 0
    je .color
    mov dl, 1
    test al, 0x80
    jz .ext_priority
    mov dl, 5
.ext_priority:
    and al, 0x7f
.color:
    test al, al
    jz .step
    cmp [ppu_z + ecx], dl
    jae .step
    mov [ppu_z + ecx], dl
    cmp dword [ppu_layer], 0
    jne .palette
    test byte [ppu_regs + 0x30], 1
    jz .palette
    mov ax, [ppu_direct_colors + eax * 2]
    jmp .plot
.palette:
    mov ax, [ppu_cgram + eax * 2]
.plot:
    mov [edi + ecx * 2], ax
    mov al, [ppu_layer_math]
    mov [ppu_line_math + ecx], al
.step:
    inc ecx
    cmp ecx, 256
    jb .next
    ret

ppu_apply_brightness:
    movzx ebp, byte [ppu_regs]
    and ebp, 15
    cmp ebp, 15
    je .done
    mov edi, [ppu_dest]
    test ebp, ebp
    jnz .dim
    xor eax, eax
    mov ecx, 256
    rep stosw
    ret
.dim:
    shl ebp, 5
    lea ebp, [ppu_brightness + ebp]
    mov ecx, 256
.pixel:
    movzx eax, word [edi]
    mov ebx, eax
    and ebx, 31
    movzx edx, byte [ebp + ebx]
    shr eax, 5
    mov ebx, eax
    and ebx, 31
    movzx ebx, byte [ebp + ebx]
    shl ebx, 5
    or edx, ebx
    shr eax, 5
    movzx eax, byte [ebp + eax]
    shl eax, 10
    or eax, edx
    stosw
    dec ecx
    jnz .pixel
.done:
    ret

align 4
ppu_plane_bits:
%assign ppu_flip 0
%rep 2
    %assign ppu_source 0
    %rep 256
        %assign ppu_packed 0
        %assign ppu_pixel 0
        %rep 8
            %if ppu_flip
                %assign ppu_bit ppu_pixel
            %else
                %assign ppu_bit (7 - ppu_pixel)
            %endif
            %assign ppu_packed ppu_packed | (((ppu_source >> ppu_bit) & 1) << (ppu_pixel * 4))
            %assign ppu_pixel ppu_pixel + 1
        %endrep
        dd ppu_packed
        %assign ppu_source ppu_source + 1
    %endrep
    %assign ppu_flip 1
%endrep

ppu_brightness:
%assign ppu_level 0
%rep 16
    %assign ppu_component 0
    %rep 32
        db (ppu_level * ppu_component + 7) / 15
        %assign ppu_component ppu_component + 1
    %endrep
    %assign ppu_level ppu_level + 1
%endrep


; Boolean results for each pair of window memberships, enabling and inversion
; bits, and combination (OR, AND, XOR, XNOR).
ppu_window_truth:
%assign ppu_win_logic 0
%rep 4
    %assign ppu_win_select 0
    %rep 16
        %assign ppu_win_where 0
        %rep 4
            %assign ppu_win_one (ppu_win_where ^ ppu_win_select) & 1
            %assign ppu_win_two ((ppu_win_where >> 1) ^ (ppu_win_select >> 2)) & 1
            %if (ppu_win_select & 2) = 0
                db ppu_win_two & ((ppu_win_select >> 3) & 1)
            %elif (ppu_win_select & 8) = 0
                db ppu_win_one
            %elif ppu_win_logic = 0
                db ppu_win_one | ppu_win_two
            %elif ppu_win_logic = 1
                db ppu_win_one & ppu_win_two
            %elif ppu_win_logic = 2
                db ppu_win_one ^ ppu_win_two
            %else
                db 1 ^ ppu_win_one ^ ppu_win_two
            %endif
            %assign ppu_win_where ppu_win_where + 1
        %endrep
        %assign ppu_win_select ppu_win_select + 1
    %endrep
    %assign ppu_win_logic ppu_win_logic + 1
%endrep


align 2
ppu_direct_colors:
%assign ppu_direct_group 0
%rep 8
    %assign ppu_direct 0
    %rep 256
        dw ((ppu_direct << 7) & 0x6000) | ((ppu_direct << 4) & 0x0380) | ((ppu_direct << 2) & 0x001c) | ((ppu_direct_group << 10) & 0x1000) | ((ppu_direct_group << 5) & 0x0040) | ((ppu_direct_group << 1) & 0x0002)
        %assign ppu_direct ppu_direct + 1
    %endrep
    %assign ppu_direct_group ppu_direct_group + 1
%endrep
