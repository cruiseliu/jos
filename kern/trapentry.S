/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>



###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */

#define CAT(x, ...) x ## __VA_ARGS__
#define PUSHEC(ec) CAT(PUSHEC_, ec)
#define PUSHEC_1
#define PUSHEC_0 pushl $0

#define TRAPHANDLER(num, ec) \
    .data; \
        .long vector##num; \
    .text; \
    .globl vector##num; \
    vector##num: \
        PUSHEC(ec); \
        pushl $(num); \
        jmp _alltraps

.data
    .align 4 // non-portable to 64-bit
    .globl vector

vector:

.text
    .align 2

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
    TRAPHANDLER(T_DIVIDE,  0)
    TRAPHANDLER(T_DEBUG,   0)
    TRAPHANDLER(T_NMI,     0)
    TRAPHANDLER(T_BRKPT,   0)
    TRAPHANDLER(T_OFLOW,   0)
    TRAPHANDLER(T_BOUND,   0)
    TRAPHANDLER(T_ILLOP,   0)
    TRAPHANDLER(T_DEVICE,  0)
    TRAPHANDLER(T_DBLFLT,  1)
    TRAPHANDLER(T_COPROC,  1)
    TRAPHANDLER(T_TSS,     1)
    TRAPHANDLER(T_SEGNP,   1)
    TRAPHANDLER(T_STACK,   1)
    TRAPHANDLER(T_GPFLT,   1)
    TRAPHANDLER(T_PGFLT,   1)
    TRAPHANDLER(T_RES,     1)
    TRAPHANDLER(T_FPERR,   0)
    TRAPHANDLER(T_ALIGN,   1)
    TRAPHANDLER(T_MCHK,    0)
    TRAPHANDLER(T_SIMDERR, 0)
.data
    .space 112 // (T_SYSCALL - T_SIMDERR - 1) * 4
.text
    TRAPHANDLER(T_SYSCALL, 0)

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
    pushl %ds // tf_padding2 and tf_ds
    pushl %es // tf_padding1 and tf_es
    pushal    // tf_regs
    
    movw $GD_KD, %ax
    movw %ax, %ds
    movw %ax, %es
    
    pushl %esp
    call trap

.global sysenter_handler

sysenter_handler:
    pushl $GD_UD | 3
    pushl %ebp
    pushfl
    pushl $GD_UT | 3
    pushl %esi
    pushl $0 // error code
    pushl $0 // trap number
    
    pushl %ds
    pushl %es
    pushal
    
    movw $GD_KD, %ax
    movw %ax, %ds
    movw %ax, %es
    
    pushl %esp
    call trap_sysenter
    pop %esp
    
    popal
    popl %es
    popl %ds
    
    movl %ebp, %ecx
    movl %esi, %edx
    
    sysexit
