// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line

#define LAB1    // print 5 args in backtrace for lab1 grading

struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
        { "backtrace", "Display stack backtrace", mon_backtrace },
        { "showmappings", "Display memory mapping status", mon_showmappings },
        { "setpage", "Set page permissions", mon_setpage },
        { "memdump", "Show memory content", mon_memdump },
        { "continue", "Continue program after breakpoint", mon_continue },
        { "si", "Step one instruction exactly", mon_si },
        { "step", "Step program until it reaches a different source line", mon_step },
        { "colortest", "Test colorful output", mon_colortest }
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

typedef union addr_t addr_t;

union addr_t {
    uint32_t addr;
    uint32_t *data;
    addr_t *ptr;
};

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
    cprintf("Stack backtrace:\n");
    addr_t ebp;
    ebp.addr = read_ebp();
    for (; ebp.ptr; ebp = *ebp.ptr) {
        struct Eipdebuginfo info;
        if (debuginfo_eip(ebp.data[1], &info)) {
            cprintf("Failed to read debug info\n");
            continue;
        }

#ifdef LAB1_GRADING
        cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
                ebp.addr, ebp.data[1], // ebp , eip
                ebp.data[2], ebp.data[3], ebp.data[4], ebp.data[5], ebp.data[6]);
#else
        cprintf("  ebp %08x  eip %08x  args", ebp.addr, ebp.data[1]);
        int i;
        for (i = 0; i < info.eip_fn_narg; i++)
            cprintf(" %08x", ebp.data[i + 2]);
        cprintf("\n");
#endif

        cprintf("         %s:%d: %.*s+%d\n",
                info.eip_file, info.eip_line,
                info.eip_fn_namelen, info.eip_fn_name,
                ebp.data[1] - info.eip_fn_addr);
    }

    return 0;
}

int mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
    if (argc == 1)
        return showmappings(NULL, 0, 0xffffffff);

    if (argc == 3) {
        uint32_t low = strtol(argv[1], NULL, 16);
        uint32_t high = strtol(argv[2], NULL, 16);
        if (low <= high) return showmappings(NULL, low, high);
    }

    cprintf("usage: showmappings low_address high_address\n");
    return 1;
}

int mon_setpage(int argc, char **argv, struct Trapframe *tf)
{
    cprintf(COLOR_YELLOW
            "WARING: setting wrong flags may crash the core, "
            "use at your own risk\n"
            COLOR_NONE);

    if (argc == 3 || argc == 4) {
        uint32_t low = strtol(argv[1], NULL, 16);
        uint32_t high = argc == 4 ? strtol(argv[2], NULL, 16) : low;
        if (low <= high) return setpage(low, high, argv[argc - 1]);
    }

    cprintf("usage: setpage low_addr [high_addr] [GSDACTUWP]\n");
    return 1;
}

int mon_memdump(int argc, char **argv, struct Trapframe *tf)
{
    cprintf(COLOR_YELLOW
            "WARNING: dump unavailable address may crash the core, "
            "use at your own risk\n"
            COLOR_NONE);

    if (argc == 3 || (argc == 4 && strcmp(argv[1], "-p") == 0)) {
        uint32_t low = strtol(argv[argc - 2], NULL, 16);
        uint32_t size = strtol(argv[argc - 1], NULL, 16);
        if (size > 0) return memdump(low, size, argc == 4);
    }

    cprintf("usage: memdump [-p] low_addr size\n");
    return 1;
}

int mon_continue(int argc, char **argv, struct Trapframe *tf)
{
    if (!tf || (tf->tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG)) {
        cprintf("No breakpoint found, trapno is %d\n", tf ? tf->tf_trapno : -1);
        return 1;
    }

    tf->tf_eflags &= ~FL_TF;

    // should never return
    env_run(curenv);

    cprintf("Failed to continue program\n");
    return 2;
}

static bool stepping = false;

int mon_step(int argc, char **argv, struct Trapframe *tf)
{
    if (!tf || (tf->tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG)) {
        cprintf("No breakpoint found, trapno is %d\n", tf ? tf->tf_trapno : -1);
        return 1;
    }

    stepping = true;

    if (step_inst(tf, true) != 0) {
        cprintf("Failed to continue program\n");
        return 2;

    } else
        return 0;
}

int mon_si(int argc, char **argv, struct Trapframe *tf)
{
    if (!tf || (tf->tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG)) {
        cprintf("No breakpoint found, trapno is %d\n", tf ? tf->tf_trapno : -1);
        return 1;
    }

    // should not return
    step_inst(tf, false);

    cprintf("Failed to continue program\n");
    return 2;
}

int mon_colortest(int argc, char **argv, struct Trapframe *tf)
{
    cprintf(COLOR_RED       "Red"
            COLOR_GREEN     "Green"
            COLOR_YELLOW    "Yellow"
            COLOR_BLUE      "Blue"
            COLOR_MAGENTA   "Magenta"
            COLOR_CYAN      "Cyan"
            "\x1b[30;47mBlack"
            "\n"COLOR_NONE);
    return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
        if (stepping && step_inst(tf, true) == 0)
            stepping = false;

	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline(COLOR_GREEN"K> "COLOR_NONE);
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
