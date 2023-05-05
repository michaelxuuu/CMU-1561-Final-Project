.globl _cas, _cs, _ss

_cas:
    mov %rsi, %rax
    mov %rdx, %rsi
    lock cmpxchgq %rsi, (%rdi)
    ret

_ss:
    mov %ss, %ax
    ret

_cs:
    mov %cs, %ax
    ret
