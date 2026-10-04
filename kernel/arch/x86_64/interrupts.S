.text
.extern rawline_interrupt_dispatch

.macro ISR_NO_ERROR number
.global rawline_isr_\number
rawline_isr_\number:
    pushq $0
    pushq $\number
    jmp rawline_interrupt_common
.endm

.macro ISR_ERROR number
.global rawline_isr_\number
rawline_isr_\number:
    pushq $\number
    jmp rawline_interrupt_common
.endm

.macro ISR_TABLE number
    .quad rawline_isr_\number
.endm

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR 8
ISR_NO_ERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR 29
ISR_ERROR 30
ISR_NO_ERROR 31
ISR_NO_ERROR 32
ISR_NO_ERROR 255

.global rawline_interrupt_common
rawline_interrupt_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    movq %rsp, %rdi
    andq $-16, %rsp
    call rawline_interrupt_dispatch
    movq %rax, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

.section .rodata
.align 8
.global rawline_isr_table
rawline_isr_table:
    ISR_TABLE 0
    ISR_TABLE 1
    ISR_TABLE 2
    ISR_TABLE 3
    ISR_TABLE 4
    ISR_TABLE 5
    ISR_TABLE 6
    ISR_TABLE 7
    ISR_TABLE 8
    ISR_TABLE 9
    ISR_TABLE 10
    ISR_TABLE 11
    ISR_TABLE 12
    ISR_TABLE 13
    ISR_TABLE 14
    ISR_TABLE 15
    ISR_TABLE 16
    ISR_TABLE 17
    ISR_TABLE 18
    ISR_TABLE 19
    ISR_TABLE 20
    ISR_TABLE 21
    ISR_TABLE 22
    ISR_TABLE 23
    ISR_TABLE 24
    ISR_TABLE 25
    ISR_TABLE 26
    ISR_TABLE 27
    ISR_TABLE 28
    ISR_TABLE 29
    ISR_TABLE 30
    ISR_TABLE 31
    ISR_TABLE 32
