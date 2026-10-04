.text
.global rawline_invoke_rwl
.type rawline_invoke_rwl, @function
rawline_invoke_rwl:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $32, %rsp
    movq rawline_rwl_recovery_active(%rip), %rax
    movq %rax, 0(%rsp)
    movq rawline_rwl_recovery_rip(%rip), %rax
    movq %rax, 8(%rsp)
    movq rawline_rwl_recovery_rsp(%rip), %rax
    movq %rax, 16(%rsp)
    movq %rsp, rawline_rwl_recovery_rsp(%rip)
    leaq .Lrwl_fault_recovery(%rip), %rax
    movq %rax, rawline_rwl_recovery_rip(%rip)
    movq $1, rawline_rwl_recovery_active(%rip)
    subq $8, %rsp
    movq %rdi, %rax
    movq %rsi, %rdi
    call *%rax
    addq $8, %rsp
    jmp .Lrwl_restore

.Lrwl_fault_recovery:
    movq rawline_rwl_recovery_rsp(%rip), %rsp
    movq 0(%rsp), %rax
    movq %rax, rawline_rwl_recovery_active(%rip)
    movq 8(%rsp), %rax
    movq %rax, rawline_rwl_recovery_rip(%rip)
    movq 16(%rsp), %rax
    movq %rax, rawline_rwl_recovery_rsp(%rip)
    movq $-1, %rax

.Lrwl_restore:
    movq 0(%rsp), %rcx
    movq %rcx, rawline_rwl_recovery_active(%rip)
    movq 8(%rsp), %rcx
    movq %rcx, rawline_rwl_recovery_rip(%rip)
    movq 16(%rsp), %rcx
    movq %rcx, rawline_rwl_recovery_rsp(%rip)
    addq $32, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

.bss
.align 8
.global rawline_rwl_recovery_active
rawline_rwl_recovery_active:
    .quad 0
.global rawline_rwl_recovery_rip
rawline_rwl_recovery_rip:
    .quad 0
.global rawline_rwl_recovery_rsp
rawline_rwl_recovery_rsp:
    .quad 0
