; Original SPC700 integration fixture, assembled as bytes for the IPL overlay.
BITS 16
ORG 0xffc0
reset:
    db 0x8f,1,0xfa         ; MOV $FA,#1, Timer 0 target
    db 0x8f,0x81,0xf1      ; MOV $F1,#$81, retain IPL and enable Timer 0
    db 0x8f,0xa7,0xf4      ; MOV $F4,#$A7, initial message to main CPU
wait_for_message:
    db 0xe4,0xf5           ; MOV A,$F5
    db 0x68,0x59           ; CMP A,#$59
    db 0xd0,wait_for_message - ($ + 2)
wait_for_timer:
    db 0xe4,0xfd           ; MOV A,$FD, read and clear Timer 0
    db 0xf0,wait_for_timer - ($ + 2)
    db 0x8f,0x96,0xf6      ; MOV $F6,#$96, reply
stay:
    db 0x2f,stay - ($ + 2)
times 62 - ($ - $$) db 0
dw reset
