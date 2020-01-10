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

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "readebp", "Display the value of ebp", mon_readebp},
	{ "time", "Counts the program's running time", mon_time},
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
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

// Lab1 only
// read the pointer to the retaddr on the stack
static uint32_t
read_pretaddr() {
    uint32_t pretaddr;
    __asm __volatile("leal 4(%%ebp), %0" : "=r" (pretaddr)); 
    return pretaddr;
}

void
do_overflow(void)
{
    cprintf("Overflow success\n");
}

void
start_overflow(void)
{
	// You should use a techique similar to buffer overflow
	// to invoke the do_overflow function and
	// the procedure must return normally.

    // And you must use the "cprintf" function with %n specifier
    // you augmented in the "Exercise 9" to do this job.

    // hint: You can use the read_pretaddr function to retrieve 
    //       the pointer to the function call return address;

    char str[256] = {};
    int nstr = 0;
    char *pret_addr;
	int ovfl_byte[8] = { 0x3b, 0xa, 0x10, 0xf0, 0x61, 0xc, 0x10, 0xf0};

	// Your code here.
	// use %n overwrite return address
    pret_addr = (char *)read_pretaddr();
	for (int i = 0 ; i < 8 ; i++) {
		char *ovfl_addr = pret_addr + i;
		memset(str, 0, sizeof(str));
		memset(str, 0xd, ovfl_byte[i]);
		cprintf("%s%n", str, ovfl_addr);
	}
}

void
overflow_me(void)
{
        start_overflow();
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	cprintf("Stack backtrace:\n");
	uintptr_t eip;
	size_t *ebp;
	size_t args[5];
	struct Eipdebuginfo info;

	eip = (uintptr_t)read_eip();
	ebp = (uint32_t *)read_ebp();
	while (ebp != (uint32_t *)0) {
		// get return address and 5 arguments from 8 bytes offset
		for (int i = 0 ; i < 5 ; i++) 
			args[i] = *(ebp + 2 + i);
		cprintf("  eip %08x  ebp %08x  args %08x %08x %08x %08x %08x\n",
				eip, ebp, args[0], args[1], args[2], args[3], args[4]);

		// get eip info from kdebug
		if (debuginfo_eip(eip, &info) < 0) {
			cprintf("Find eip information failed\n");
			return -1;
		}
		char buf[512];
		strncpy(buf, info.eip_fn_name, info.eip_fn_namelen);
		buf[info.eip_fn_namelen] = '\0';
		cprintf("\t %s:%d %s+%d\n", info.eip_file, info.eip_line, buf, eip - info.eip_fn_addr);
		
		// get base pointer of caller frame
		ebp = (size_t *)*ebp;
		eip = *(ebp + 1);
	}

	overflow_me();
    cprintf("Backtrace success\n");
	return 0;
}

int
mon_readebp(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t ebp = read_ebp();
	cprintf("$ebp = 0x%08x\n", ebp);
	return 0;
}

int
mon_time(int argc, char **argv, struct Trapframe *tf)
{
	if (argc < 2) {
		cprintf("The usage is: time [command]\n");
		return 0;
	}

	for (int i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[1], commands[i].name) == 0) {
			uint64_t tsc_start = read_tsc();
			int ret = commands[i].func(argc - 1, ++argv, tf);
			uint64_t tsc_end = read_tsc();
			cprintf("kerninfo cycles: %llu\n", tsc_end - tsc_start);
			return ret;
		}
	}

	cprintf("Unknown command '%s'\n", argv[1]);
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
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
