.section .text
.globl clone_trampoline
.type clone_trampoline, @function
clone_trampoline:
    pop %rdi
    pop %rax
    call *%rax
    mov %eax, %edi
    mov $60, %eax
    syscall
    hlt
.size clone_trampoline, . - clone_trampoline
