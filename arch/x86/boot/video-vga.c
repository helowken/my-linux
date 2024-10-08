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
 * Common all-VGA modes
 */

#include "boot.h"
#include "video.h"

static struct mode_info vga_modes[] = {
	{ VIDEO_80x25,  80, 25, 0 },
	{ VIDEO_8POINT, 80, 50, 0 },
	{ VIDEO_80x43,  80, 43, 0 },
	{ VIDEO_80x28,  80, 28, 0 },
	{ VIDEO_80x30,  80, 30, 0 },
	{ VIDEO_80x34,  80, 34, 0 },
	{ VIDEO_80x60,  80, 60, 0 },
};

static struct mode_info ega_modes[] = {
	{ VIDEO_80x25,  80, 25, 0 },
	{ VIDEO_8POINT, 80, 43, 0 }
};

static struct mode_info cga_modes[] = {
	{ VIDEO_80x25,  80, 25, 0 },
};

static __videocard video_vga;

/* Set basic 80x25 mode */
static u8 vga_set_basic_mode(void)
{
	struct biosregs ireg, oreg;
	u16 ax;
	u8 rows;
	u8 mode;

	initregs(&ireg);

	ax = 0x0f00;
	intcall(0x10, &ireg, &oreg);
	mode = oreg.al;

	set_fs(0);
	rows = rdfs8(0x484);	/* rows-1 */

	/* AH: width of screen, 0x50 means width=80 */
	if ((oreg.ax == 0x5003 || oreg.ax == 0x5007) &&
				(rows == 0 || rows == 24))
	  return mode;

	if (mode != 3 && mode != 7)
	  mode = 3;

	/* Set the mode */
	ireg.ax = mode;		/* AH=0: set mode */
	intcall(0x10, &ireg, NULL);
	do_restore = 1;
	return mode;
}

static void vga_set_8font(void)
{
	/* Set 8x8 font - 80x43 on EGA, 80x50 on VGA */
	struct biosregs ireg;

	initregs(&ireg);

	/* Set 8x8 font */
	ireg.ax = 0x1112;
	/* ireg.bl = 0; */
	intcall(0x10, &ireg, NULL);

	/* Use alternate print screen */
	ireg.ax = 0x1200;
	ireg.bl = 0x20;
	intcall(0x10, &ireg, NULL);

	/* Turn off cursor emulation */
	ireg.ax = 0x1201;
	ireg.bl = 0x34;
	intcall(0x10, &ireg, NULL);

	/* Cursor is scan lines 6-7 */
	ireg.ax = 0x0100;
	ireg.cx = 0x0607;
	intcall(0x10, &ireg, NULL);
}

static void vga_set_14font(void)
{
	/* Set 9x14 font - 80x28 on VGA */
	struct biosregs ireg;

	initregs(&ireg);

	/* Set 9x14 font */
	ireg.ax = 0x1111;
	/* ireg.bl = 0; */
	intcall(0x10, &ireg, NULL);

	/* Turn off cursor emulation */
	ireg.ax = 0x1201;
	ireg.bl = 0x34;
	intcall(0x10, &ireg, NULL);

	/* Cursor is scan lines 11-12 */
	ireg.ax = 0x0100;
	ireg.cx = 0x0b0c;
	intcall(0x10, &ireg, NULL);
}

static void vga_set_80x43(void)
{
	/* Set 80x43 mode on VGA (not EGA) */
	struct biosregs ireg;

	initregs(&ireg);

	/* Set 350 scans */
	ireg.ax = 0x1201;
	ireg.bl = 0x30;
	intcall(0x10, &ireg, NULL);

	/* Reset video mode */
	ireg.ax = 0x0003;
	intcall(0x10, &ireg, NULL);

	vga_set_8font();
}

/* I/O address of the VGA CRTC */
u16 vga_crtc(void)
{
	/* refer to:
	 *  http://www.techhelpmanual.com/900-video_graphics_array_i_o_ports.html
	 *  https://wiki.osdev.org/VGA_Hardware
	 *
	 * Port 0x3C2
	 * This is the miscellaneous output register. It uses port 0x3C2 for 
	 * writing, and 0x3CC for reading. 
	 *
	 * Port 0x3D4 has some extra requirements - it requires bit 0 of the 
	 * Miscellaneous Output Register to be set before it responds to this 
	 * address (if cleared, these ports appears at 0x3B4). Also, registers 0-7 
	 * of 0x3D4 are write protected by the protect bit (bit 7 of index 0x11)
	 */
	return (inb(0x3cc) & 1) ? 0x3d4 : 0x3b4;
}

static void vga_set_480_scanlines(void)
{
	u16 crtc;		/* CRTC base address */
	u8 csel;		/* CRTC miscellaneous output register */

	crtc = vga_crtc();

	/* refer to "List of register settings" and "Port 0x3C4, 0x3CE, 0x3D4" in:
	 *  https://wiki.osdev.org/VGA_Hardware
	 *
	 * The index byte is written to the port given, then the data byte can 
	 * be read/written from port+1. Some programs use a single 16-bit access 
	 * instead of two byte accesses for writing, which does effectively the same.
	 */
	out_idx(0x0c, crtc, 0x11);	/* Vertical sync end, unlock CR0-7 */
	out_idx(0x0b, crtc, 0x06);	/* Vertical total */
	out_idx(0x3e, crtc, 0x07);	/* Vertical overflow */
	out_idx(0xea, crtc, 0x10);	/* Vertical sync start */
	out_idx(0xdf, crtc, 0x12);	/* Vertical display end */
	out_idx(0xe7, crtc, 0x15);	/* Vertical blank start */
	out_idx(0x04, crtc, 0x16);	/* Vertical blank end */
	csel = inb(0x3cc);
	csel &= 0x0d;	/* keep Clock Select and bit-0 */
	csel |= 0xe2;	/* for 640x480, use 0xE3 */
	outb(csel, 0x3c2);
}

static void vga_set_vertical_end(int lines)
{
	u16 crtc;		/* CRTC base address */
	u8 ovfw;		/* CRTC overflow register */
	int end = lines - 1;

	crtc = vga_crtc();

	ovfw = 0x3c | ((end >> (8-1)) & 0x02) | ((end >> (9-6)) & 0x40);

	out_idx(ovfw, crtc, 0x07);	/* Vertical overflow */
	out_idx(end,  crtc, 0x12);	/* Vertical display end */
}

static void vga_set_80x30(void)
{
	vga_set_480_scanlines();
	vga_set_vertical_end(30*16);
}

static void vga_set_80x34(void)
{
	vga_set_480_scanlines();
	vga_set_14font();
	vga_set_vertical_end(34*14);
}

static void vga_set_80x60(void)
{
	vga_set_480_scanlines();
	vga_set_8font();
	vga_set_vertical_end(60*8);
}

static int vga_set_mode(struct mode_info *mode)
{
	/* Set the basic mode */
	vga_set_basic_mode();

	/* Override a possibly broken BIOS */
	force_x = mode->x;
	force_y = mode->y;

	/* refer to:
	 * http://www.techhelpmanual.com/151-int_10h_11h__ega_vga_character_generator_functions.html
	 */
	switch (mode->mode) {
		case VIDEO_80x25:
			break;
		case VIDEO_8POINT:
			vga_set_8font();
			break;
		case VIDEO_80x43:
			vga_set_80x43();
			break;
		case VIDEO_80x28:
			vga_set_14font();
			break;
		case VIDEO_80x30:
			vga_set_80x30();
			break;
		case VIDEO_80x34:
			vga_set_80x34();
			break;
		case VIDEO_80x60:
			vga_set_80x60();
			break;
	}

	return 0;
}

/*
 * Note: this probe includes basic information required by all
 * systems.  It should be executed first, by making sure
 * video-vga.c is listed first in the Makefile.
 */
static int vga_probe(void)
{
	static const char *card_name[] = {
		"CGA/MDA/HGC", "EGA", "VGA"
	};
	static struct mode_info *mode_lists[] = {
		cga_modes,
		ega_modes,
		vga_modes
	};
	static int mode_count[] = {
		sizeof(cga_modes) / sizeof(struct mode_info),
		sizeof(ega_modes) / sizeof(struct mode_info),
		sizeof(vga_modes) / sizeof(struct mode_info),
	};

	struct biosregs ireg, oreg;

	initregs(&ireg);

	/* refer to:
	 * http://www.techhelpmanual.com/168-int_10h_12h_bl_10h__get_ega_information.html
	 */ 
	ireg.ax = 0x1200;
	ireg.bl = 0x10;		/* Check EGA/VGA */
	intcall(0x10, &ireg, &oreg);

#ifndef _WAKEUP
	boot_params.screen_info.orig_video_ega_bx = oreg.bx;
#endif
	
	/* If we have MDA/CGA/HGC then BL will be unchanged at 0x10 */
	if (oreg.bl != 0x10) {
		/* EGA/VGA */

		/* refer to:
		 * http://www.techhelpmanual.com/177-int_10h_1ah__set_or_query_display_combination_code.html
		 */
		ireg.ax = 0x1a00;
		intcall(0x10, &ireg, &oreg);

		if (oreg.al == 0x1a) {	/* success */
			adapter = ADAPTER_VGA;
#ifndef _WAKEUP
			boot_params.screen_info.orig_video_isVGA = 1;
#endif
		} else {
			adapter = ADAPTER_EGA;
		}
	} else {
		adapter = ADAPTER_CGA;
	}

	video_vga.modes = mode_lists[adapter];
	video_vga.card_name = card_name[adapter];
	return mode_count[adapter];
}

static __videocard video_vga = {
	.card_name = "VGA",
	.probe     = vga_probe,
	.set_mode  = vga_set_mode
};





