.section .text
.globl _start
.type _start, @function
_start:
    mov (%rsp), %edi
    lea 8(%rsp), %rsi
    and $-16, %rsp
    xor %ebp, %ebp
    call crom_main
    mov %eax, %edi
    mov $231, %eax
    syscall
    hlt
.size _start, . - _start
