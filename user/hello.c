// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	cprintf(COLOR_CYAN"hello, world\n"COLOR_NONE);
	cprintf(COLOR_CYAN"i am environment %08x\n"COLOR_NONE, thisenv->env_id);
}
