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
#define PUSHEC_0
#define PUSHEC_1 pushl $0;

#define TRAPHANDLER_X(name, num, ec) \
        .globl name; \
        .type name, @function; \
        .align 2; \
        name: \
        PUSHEC(ec) \
        pushl $(num); \
        jmp _alltraps

#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
//TRAPHANDLER_X(th0 , T_DIVIDE , 0);
//TRAPHANDLER_X(th1 , T_DEBUG  , 0);
//TRAPHANDLER_X(th2 , T_NMI    , 0);
//TRAPHANDLER_X(th3 , T_BRKPT  , 0);
//TRAPHANDLER_X(th4 , T_OFLOW  , 0);
//TRAPHANDLER_X(th5 , T_BOUND  , 0);
//TRAPHANDLER_X(th6 , T_ILLOP  , 0);
//TRAPHANDLER_X(th7 , T_DEVICE , 0);
//TRAPHANDLER_X(th8 , T_DBLFLT , 1);
//TRAPHANDLER_X(th10, T_TSS    , 1);
//TRAPHANDLER_X(th11, T_SEGNP  , 1);
//TRAPHANDLER_X(th12, T_STACK  , 1);
//TRAPHANDLER_X(th13, T_GPFLT  , 1);
//TRAPHANDLER_X(th14, T_PGFLT  , 1);
//TRAPHANDLER_X(th16, T_FPERR  , 0);
//TRAPHANDLER_X(th17, T_ALIGN  , 1);
//TRAPHANDLER_X(th18, T_MCHK   , 0);
//TRAPHANDLER_X(th19, T_SIMDERR, 0);
//TRAPHANDLER_X(th48, T_SYSCALL, 0);

TRAPHANDLER_NOEC(th0, T_DIVIDE);
TRAPHANDLER_NOEC(th1, T_DEBUG);
TRAPHANDLER_NOEC(th2, T_NMI);
TRAPHANDLER_NOEC(th3, T_BRKPT);
TRAPHANDLER_NOEC(th4, T_OFLOW);
TRAPHANDLER_NOEC(th5, T_BOUND);
TRAPHANDLER_NOEC(th6, T_ILLOP);
TRAPHANDLER_NOEC(th7, T_DEVICE);
TRAPHANDLER(th8, T_DBLFLT);
TRAPHANDLER(th10, T_TSS);
TRAPHANDLER(th11, T_SEGNP);
TRAPHANDLER(th12, T_STACK);
TRAPHANDLER(th13, T_GPFLT);
TRAPHANDLER(th14, T_PGFLT);
TRAPHANDLER_NOEC(th16, T_FPERR);
TRAPHANDLER(th17, T_ALIGN);
TRAPHANDLER_NOEC(th18, T_MCHK);
TRAPHANDLER_NOEC(th19, T_SIMDERR);
TRAPHANDLER_NOEC(th48, T_SYSCALL);

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:

pushw $0;  // tf_padding2
pushw %ds; // tf_ds
pushw $0;  // tf_padding1
pushw %es; // tf_es
pushal;    // tf_regs

movw $GD_KD, %ax;
movw %ax, %ds;
movw %ax, %es;

pushl %esp;

call trap;
