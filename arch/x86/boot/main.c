/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Main module for the real-mode kernel code
 */

#include "boot.h"

struct boot_params boot_params __attribute__((aligned(16)));

char *HEAP = _end;
char *heap_end = _end;		/* Default end of heap = no heap */

static void copy_boot_params(void) 
{
	BUILD_BUG_ON(sizeof boot_params != 4096);
	memcpy(&boot_params.hdr, &hdr, sizeof hdr);
}

static void init_heap(void) 
{
	char *stack_end;

	if (boot_params.hdr.loadflags & CAN_USE_HEAP) {
		asm("leal %P1(%%esp),%0"
			: "=r" (stack_end) : "i" (-STACK_SIZE));

		heap_end = (char *) ((size_t) boot_params.hdr.heap_end_ptr + 0x200);
		if (heap_end > stack_end)
		  heap_end = stack_end;
	} else {
		/* Boot protocol 2.00 only, no heap available */
		puts("WARNING: Ancient bootloader, some functionality "
			 "may be limited!\n");
	}
}

static void set_bios_mode(void)
{
#ifdef CONFIG_X86_64
	struct biosregs ireg;

	initregs(&ireg);

/* "You could do it slightly earlier by intercepting bios int 0x15 eax=0xec00 ebx=2. 
 * The kernel issues that to tell the BIOS it is 64bit. That will only work if the 
 * boot loader does not skip the real mode code."
 *
 * Appearantly, the BIOS likes to know wheter a 64-bit OS is running.
 */
	ireg.ax = 0xec00;	/* declare target operating mode */
	ireg.bx = 2;	/* long mode */
	intcall(0x15, &ireg, NULL);
#endif
}

/* 
 * Set the keyboard repeat rate to maximum.  Unclear why this
 * is done here; this might be possible to kill off as stale code.
 */
static void keyboard_set_repeat(void)
{
	struct biosregs ireg;

	initregs(&ireg);
	ireg.ax = 0x0305;
	intcall(0x16, &ireg, NULL);
}

void main(void) 
{
	/* First, copy the boot header into the "zeropage" */
	copy_boot_params();

	/* End of heap check */
	init_heap();

	/* make sure we have all the proper CPU support */
	if (validate_cpu()) {
		puts("Unable to boot - please use a kernel appropriate "
			 "for your CPU.\n");
		die();
	}

	/* Tell the BIOS what CPU mode we intend to run in. */
	set_bios_mode();

	/* Detect memory layout */
	detect_memory();

	/* Set keyboard repeat rate (why?) */
	keyboard_set_repeat();

	/* TODO query MCA*/
	/* TODO query Intel SpeedStep Information */

	/* Set the video mode */
	set_video();

	/* Parse command line for 'quiet' and pass it to decompressor. */
	if (cmdline_find_option_bool("quiet"))
	  boot_params.hdr.loadflags |= QUIET_FLAG;

	/* Do the last things and invoke protected mode */
	go_to_protected_mode();
}
