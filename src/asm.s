.globl _cas
.globl _dcas

_cas:
    mov %rsi, %rax
    mov %rdx, %rsi
    lock cmpxchgq %rsi, (%rdi)
    ret

_dcas:
    mov 4(%rsi), %eax # higher half - old_val[2]
    shl $32, %rax
    mov (%rsi), %ebx # lower half - old_val[1]
    or %rbx, %rax

    mov 4(%rdx), %esi # higher half - new_val[2]
    shl $32, %rsi
    mov (%rdx), %ebx /* lower half - old_val[1] */
    or %rbx, %rsi
    
    lock cmpxchgq %rsi, (%rdi)
    ret
