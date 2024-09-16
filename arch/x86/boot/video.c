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
 * Select video mode
 */

#include "boot.h"
#include "video.h"
#include "vesa.h"

static void store_cursor_position(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ah = 0x03;
	intcall(0x10, &ireg, &oreg);

	boot_params.screen_info.orig_x = oreg.dl;
	boot_params.screen_info.orig_y = oreg.dh;
}

static void store_video_mode(void)
{
	struct biosregs ireg, oreg;

	/* N.B.: the saving of the video page here is a bit silly,
	 * since we pretty much assume page 0 everywhere. */
	initregs(&ireg);
	ireg.ah = 0x0f;
	intcall(0x10, &ireg, &oreg);

	/* Not all BIOSes are clean with respect to the top bit */
	boot_params.screen_info.orig_video_mode = oreg.al & 0x7f;
	boot_params.screen_info.orig_video_page = oreg.bh;
}

/*
 * Store the video mode parameters for later usage by the kernel.
 * This is done by asking the BIOS except for the rows/columns
 * parameters in the default 80x25 mode -- these are set directly,
 * because some very obscure BIOSes supply insane values.
 */
static void store_mode_params(void)
{
	u16 font_size;
	int x, y;

	/* For graphics mode, it is up the mode-setting driver
	 * (currently only video-vesa.c) to store the parameters */
	if (graphic_mode)
	  return;

	store_cursor_position();
	store_video_mode();

	/* refer to:
	 * http://www.techhelpmanual.com/89-video_memory_layouts.html
	 */
	if (boot_params.screen_info.orig_video_mode == 0x07) {
		/* MDA, HGC, or VGA in monochrome mode */
		video_segment = 0xb000;
	} else {
		/* CGA, EGA, VGA and so forth */
		video_segment = 0xb800;
	}

	/* refer to "40:0085" in: 
	 * http://www.techhelpmanual.com/93-rom_bios_variables.html */
	set_fs(0);
	font_size = rdfs16(0x485);	/* Font size, BIOS area */
	boot_params.screen_info.orig_video_points = font_size;

	x = rdfs16(0x44a);
	y = (adapter == ADAPTER_CGA) ? 25 : rdfs8(0x484) + 1;
	
	if (force_x)
	  x = force_x;
	if (force_y)
	  y = force_y;

	boot_params.screen_info.orig_video_cols = x;
	boot_params.screen_info.orig_video_lines = y;
}

/* Save screen content to the heap */
static struct saved_screen {
	int x, y;
	int curx, cury;
	u16 *data;
} saved;

static void save_screen(void)
{
	/* Should be called after store_mode_params() */
	saved.x = boot_params.screen_info.orig_video_cols;
	saved.y = boot_params.screen_info.orig_video_lines;
	saved.curx = boot_params.screen_info.orig_x;
	saved.cury = boot_params.screen_info.orig_y;

	if (! heap_free(saved.x * saved.y * sizeof(u16) + 512))
	  return;	/* Not enough heap to save the screen */

	saved.data = GET_HEAP(u16, saved.x * saved.y);

	/* refer to: "Accessing Text Mode Video Memory" in
	 * http://www.techhelpmanual.com/89-video_memory_layouts.html
	 */
	set_fs(video_segment);
	/* src = video_segment:0 */
	copy_from_fs(saved.data, 0, saved.x * saved.y * sizeof(u16));
}

static void restore_screen(void)
{
	/* Should be called after store_mode_params() */
	int xs = boot_params.screen_info.orig_video_cols;
	int ys = boot_params.screen_info.orig_video_lines;
	int y;
	addr_t dst = 0;
	u16 *src = saved.data;
	struct biosregs ireg;

	if (graphic_mode)
	  return;		/* Can't restore onto a graphic mode */

	if (! src)
	  return;		/* No saved screen contents */

	/* Restore screen contents */

	set_fs(video_segment);
	for (y = 0; y < ys; y++) {
		int npad;

		if (y < saved.y) {
			int copy = (xs < saved.x) ? xs : saved.x;
			
			copy_to_fs(dst, src, copy * sizeof(u16));
			dst += copy * sizeof(u16);
			src += saved.x;
			npad = (xs < saved.x) ? 0 : xs - saved.x;
		} else {
			npad = xs;
		}

		/* Writes "npad" blank characters to 
		   video_segment:dst and advances dst */
		/* cx = npad
		   es = video_segment
		   di = dst
		   eax = 0x07200720 (0x20 is space, 0x07 is char attrs)
		   stosw means: cx % 2 != 0, store ax to es:di
		   rep;stols means: store eax to es:di 
		*/
		asm volatile("pushw %%es; "
					"movw %2,%%es; "
					"shrw %%cx; "
					"jnc 1f; "
					"stosw \n\t"
					"1: rep;stosl; "
					"popw %%es"
					: "+D" (dst), "+c" (npad)
					: "bdS" (video_segment),
					  "a" (0x07200720));
	}

	/* Restore cursor position */
	initregs(&ireg);
	ireg.ah = 0x02;		/* Set cursor position */
	ireg.dh = saved.cury;
	ireg.dl = saved.curx;
	intcall(0x10, &ireg, NULL);
}

void set_video(void)
{
	u16 mode = boot_params.hdr.vid_mode;

	RESET_HEAP();

	store_mode_params();
	save_screen();
	probe_cards(0);

	for (;;) {
		if (mode == ASK_VGA) {
			printf("Not support ASK_VGA now: %x\n", mode);
			mode = NORMAL_VGA;
			/* TODO mode = mode_menu(); */
		}

		if (! set_mode(mode))
		  break;

		printf("Undefined video mode number: %x\n", mode);
		mode = ASK_VGA;
	}
	boot_params.hdr.vid_mode = mode;
	/* TODO vesa_store_edid(); */
	store_mode_params();

	if (do_restore)
	  restore_screen();
}


