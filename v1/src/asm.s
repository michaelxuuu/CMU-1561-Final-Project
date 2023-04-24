.globl _cas32, _cas64

_cas32:
    mov %esi, %eax
    mov %edx, %esi
    lock cmpxchgl %esi, (%rdi)
    ret

_cas64:
    mov %rsi, %rax
    mov %rdx, %rsi
    lock cmpxchgq %rsi, (%rdi)
    ret

_dcas32:
    mov 4(%rsi), %eax   /* higher half - oldval[2] */
    shl $32, %rax
    mov (%rsi), %ebx    /* lower half - oldval[1] */
    or %rbx, %rax

    mov 4(%rdx), %esi   /* higher half - newval[2] */
    shl $32, %rsi
    mov (%rdx), %ebx    /* lower half - oldval[1] */
    or %rbx, %rsi
    
    lock cmpxchgq %rsi, (%rdi)
    ret


/* int _dcas64(void *addr, long oldval[2], newval[2], actual[2]) */
/* rdi - addr */
/* rsi - oldval */
/* rdx - newval */
/* rcx - actual */
_dcas64:
    /* rcx:rbx */
    mov (%rdx), %rbx
    mov 8(%rdx), %rcx

    /* rdx:rax */
    mov (%rsi), %rax
    mov 8(%rsi), %rdx

    lock cmpxchg16b (%rdi)

    mov $1, %rax /* succ */
    jz .succ
    mov $0, %rax /* fail */
    mov %rax, (%rcx)
    mov %rdx, 8(%rcx)
.succ:
    ret
