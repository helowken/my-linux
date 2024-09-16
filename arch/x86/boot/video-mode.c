/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007-2008 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/i386/boot/video-mode.c
 *
 * Set the video mode.  This is separated out into a different
 * file in order to be shared with the ACPI wakeup code.
 */

#include "boot.h"
#include "video.h"
#include "vesa.h"

/*
 * Common variables
 */
int adapter;			/* 0=CGA/MDA/HGC, 1=EGA, 2=VGA+ */
u16 video_segment;
int force_x, force_y;	/* Don't query the BIOS for cols/rows */

int do_restore;		/* Screen contents changed during mode flip */
int graphic_mode;	/* Graphic mode with linear frame buffer */

/* Probe the video drivers and have them generate their mode lists. */
void probe_cards(int unsafe)
{
	struct card_info *card;
	static u8 probed[2];

	if (probed[unsafe])
	  return;

	probed[unsafe] = 1;

	for (card = video_cards; card < video_cards_end; card++) {
		if (card->unsafe == unsafe) {
			if (card->probe)
			  card->nmodes = card->probe();
			else
			  card->nmodes = 0;
		}
	}
}

/* Test if a mode is defined */
int mode_defined(u16 mode)
{
	struct card_info *card;
	struct mode_info *mi;
	int i;

	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			if (mi->mode == mode)
			  return 1;
		}
	}

	return 0;
}

/* Set mode (without recalc) */
static int raw_set_mode(u16 mode, u16 *real_mode)
{
	int nmode, i;
	struct card_info *card;
	struct mode_info *mi;

	/* Drop the recalc bit if set */
	mode &= ~VIDEO_RECALC;

	/* Scan for mode based on fixed ID, position, or resolution */
	nmode = 0;
	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			int visible = mi->x || mi->y;

			if ((mode == nmode && visible) ||
						mode == mi->mode ||
						mode == (mi->y << 8) + mi->x) {
				*real_mode = mi->mode;
				return card->set_mode(mi);
			}

			if (visible)
			  nmode++;
		}
	}

	/* Nothing found?  Is it an "exceptional" (unprobed) mode? */
	for (card = video_cards; card < video_cards_end; card++) {
		if (mode >= card->xmode_first &&
					mode < card->xmode_first + card->xmode_n) {
			struct mode_info mix;
			
			*real_mode = mix.mode = mode;
			mix.x = mix.y = 0;
			return card->set_mode(&mix);
		}
	}

	/* Otherwise, failure... */
	return -1;
}

/* Set mode (with recalc if specified) */
int set_mode(u16 mode)
{
	int rv;
	u16 real_mode;

	/* Very special mode numbers... */
	if (mode == VIDEO_CURRENT_MODE)
	  return 0;		/* Nothing to do... */
	else if (mode == NORMAL_VGA)
	  mode = VIDEO_80x25;
	else if (mode == EXTENDED_VGA)
	  mode = VIDEO_8POINT;

	rv = raw_set_mode(mode, &real_mode);
	if (rv)
	  return rv;

	/* TODO
	if (mode & VIDEO_RECALC)
	  vga_recalc_vertical();
	*/

	/* Svae the canonical mode number for the kernel, not
	 * an alias, size sepcification or menu position */
#ifndef _WAKEUP
	boot_params.hdr.vid_mode = real_mode;
#endif
	return 0;
}
