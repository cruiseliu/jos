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

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:

pushw $0;  // tf_padding2
pushw %ds; // tf_ds
pushw $0;  // tf_padding1
pushw %es; // tf_es
pushal;   // tf_regs

popl %eax;
pushl $0xdeadbeef;

movw $GD_KD, %ax;
movw %ax, %ds;
movw %ax, %es;

pushl %esp;

call trap;

//popl %esp
//popa
//popw %es
//addl $2, %esp
//popw %ds
//addl $10, %esp
//iret
