; Transparent entry observers. Preserve every volatile argument register, flags,
; floating-point arguments and the original stack, then tail-call the trampoline.
; This avoids guessing the signature of research-only camera/attack observations.
OPTION CASEMAP:NONE
EXTERN AsterCameraTrampoline:QWORD
EXTERN AsterAttackTrampoline:QWORD
EXTERN AsterObserveCamera:PROC
EXTERN AsterObserveAttack:PROC
.code
OBSERVER MACRO name:req, callback:req, trampoline:req
PUBLIC name
name PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 088h
    movdqu XMMWORD PTR [rsp+020h], xmm0
    movdqu XMMWORD PTR [rsp+030h], xmm1
    movdqu XMMWORD PTR [rsp+040h], xmm2
    movdqu XMMWORD PTR [rsp+050h], xmm3
    movdqu XMMWORD PTR [rsp+060h], xmm4
    movdqu XMMWORD PTR [rsp+070h], xmm5
    mov rcx, QWORD PTR [rsp+0B0h]
    mov rdx, QWORD PTR [rsp+0A8h]
    mov r8, QWORD PTR [rsp+0A0h]
    mov r9, QWORD PTR [rsp+098h]
    call callback
    movdqu xmm0, XMMWORD PTR [rsp+020h]
    movdqu xmm1, XMMWORD PTR [rsp+030h]
    movdqu xmm2, XMMWORD PTR [rsp+040h]
    movdqu xmm3, XMMWORD PTR [rsp+050h]
    movdqu xmm4, XMMWORD PTR [rsp+060h]
    movdqu xmm5, XMMWORD PTR [rsp+070h]
    add rsp, 088h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp QWORD PTR [trampoline]
name ENDP
ENDM
OBSERVER AsterCameraObserverEntry, AsterObserveCamera, AsterCameraTrampoline
OBSERVER AsterAttackObserverEntry, AsterObserveAttack, AsterAttackTrampoline
END
