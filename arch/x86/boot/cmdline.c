/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Simple command-line parser for early boot.
 */

#include "boot.h"

static inline int myisspace(u8 c)
{
	return c <= ' ';	/* Close enough approximation */
}

/* 
 * Find a non-boolean option, that is , "option=argument".  In accordance
 * with standard Linux practice, if this option is repeated, this returns
 * the last instance on the command line.
 *
 * Returns the length of the argument (regradless of if it was
 * truncated to fit in the buffer), or -1 on not found.
 */
int cmdline_find_option(const char *option, char *buffer, int bufsize)
{
	u32 cmdline_ptr = boot_params.hdr.cmd_line_ptr;
	addr_t cptr;
	char c;
	int len = -1;
	const char *opptr = NULL;
	char *bufptr = buffer;
	enum {
		st_wordstart,	/* Start of word/after whitespace */
		st_wordcmp,	/* Comparing this word */
		st_wordskip,	/* Miscompare, skip */
		st_bufcpy	/* Copying this to buffer */
	} state = st_wordstart;

	if (! cmdline_ptr || cmdline_ptr >= 0x100000)
	  return -1;	/* No command line, or inaccessible */

	/* convert 32-bit addr to fs:off */
	cptr = cmdline_ptr & 0xf;
	set_fs(cmdline_ptr >> 4);

	while (cptr < 0x10000 && (c = rdfs8(cptr++))) {
		switch (state) {
			case st_wordstart:
				if (myisspace(c))
				  break;

				/* else */
				state = st_wordcmp;
				opptr = option;
				/* fall througth */

			case st_wordcmp:
				if (c == '=' && ! *opptr) {
					len = 0;
					bufptr = buffer;
					state = st_bufcpy;
				} else if (myisspace(c)) {
					state = st_wordstart;
				} else if (c != *opptr++) {
					state = st_wordskip;
				}
				break;

			case st_wordskip:
				if (myisspace(c))
				  state = st_wordstart;
				break;

			case st_bufcpy:
				if (myisspace(c)) {
					state = st_wordstart;
				} else {
					if (len < bufsize - 1)
					  *bufptr++ = c;
					len++;
				}
				break;
		}
	}

	if (bufsize)
	  *bufptr = '\0';

	return len;
}

/* 
 * Find a boolean option (like quiet,noapic,nosmp....)
 *
 * Returns the position of that option (starts counting with 1)
 * or 0 on not found
 */
int cmdline_find_option_bool(const char *option)
{
	u32 cmdline_ptr = boot_params.hdr.cmd_line_ptr;
	addr_t cptr;
	char c;
	int pos = 0, wstart = 0;
	const char *opptr = NULL;
	enum {
		st_wordstart,	/* Start of word/after whitespace */
		st_wordcmp,	/* Comparing this word */
		st_wordskip,	/* Miscompare, skip */
	} state = st_wordstart;

	if (! cmdline_ptr || cmdline_ptr >= 0x100000)
	  return -1;	/* No command line, or inaccessible */

	/* convert 32-bit addr to fs:off */
	cptr = cmdline_ptr & 0xf;
	set_fs(cmdline_ptr >> 4);

	while (cptr < 0x10000) {
		c = rdfs8(cptr++);
		pos++;

		switch (state) {
			case st_wordstart:
				if (! c)
				  return 0;
				else if (myisspace(c))
				  break;

				state = st_wordcmp;
				opptr = option;
				wstart = pos;
				/* fall through */

			case st_wordcmp:
				if (! *opptr) {
					if (! c || myisspace(c))
					  return wstart;
					else
					  state = st_wordskip;
				} else if (! c)
				  return 0;
				else if (c != *opptr++)
				  state = st_wordskip;
				break;

			case st_wordskip:
				if (! c)
				  return 0;
				else if (myisspace(c))
				  state = st_wordstart;
				break;
		}
	}

	return 0;	/* Buffer overrun */
}




