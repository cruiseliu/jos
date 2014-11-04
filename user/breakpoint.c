// program to cause a breakpoint trap

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
    cprintf("======================\n");
	asm volatile("int $3");
        cprintf("------------------------\n");
}

