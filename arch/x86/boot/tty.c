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
 * Very simple screen I/O
 * XXX: Probably should add very simple serial I/O?
 */

#include "boot.h"

/*
 * These functions are in .inittext so they can be used to signal
 * error during initialization.
 */

void __attribute__((section(".inittext"))) putchar(int ch)
{
	struct biosregs ireg;

	if (ch == '\n')
	  putchar('\r');  /* \n -> \r\n */

	initregs(&ireg);
	ireg.bx = 0x0007;
	ireg.cx = 0x0001;
	ireg.ah = 0x0e;
	ireg.al = ch;
	intcall(0x10, &ireg, NULL);
}

void __attribute__((section(".inittext"))) puts(const char *str)
{
	while (*str) {
		putchar(*str++);
	}
}




