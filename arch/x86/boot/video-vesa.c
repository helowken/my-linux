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
 * VESA text modes
 */

#include "boot.h"
#include "video.h"
#include "vesa.h"

/* VESA information */
static struct vesa_general_info vginfo;
static struct vesa_mode_info vminfo;

static __videocard video_vesa;

#ifndef _WAKEUP
static void vesa_store_mode_params_graphics(void);
#else /* _WAKEUP */
static inline void vesa_store_mode_params_graphics(void) {}
#endif /* _WAKEUP */

static int vesa_probe(void)
{
	struct biosregs ireg, oreg;
	u16 mode;
	addr_t mode_ptr;
	struct mode_info *mi;
	int nmodes = 0;

	/* modes is a ptr, so n=0 in GET_HEAP */
	video_vesa.modes = GET_HEAP(struct mode_info, 0);

	initregs(&ireg);
	ireg.ax = 0x4f00;
	ireg.di = (size_t) &vginfo;
	intcall(0x10, &ireg, &oreg);

	
	if (oreg.ax != 0x004f ||
			vginfo.signature != VESA_MAGIC ||
			vginfo.version < 0x0102)
	  return 0;		/* Not present */

	set_fs(vginfo.video_mode_ptr.seg);
	mode_ptr = vginfo.video_mode_ptr.off;

	while ((mode = rdfs16(mode_ptr)) != 0xffff) {
		mode_ptr += 2;

		if (! heap_free(sizeof(struct mode_info)))
		  break;	/* Heap full, can't save mode info */

		/* refer to:
		 * http://www.techhelpmanual.com/85-vesa_svga_video_modes.html
		 */
		if (mode & ~0x1ff)
		  continue;

		memset(&vminfo, 0, sizeof vminfo);	/* Just in case... */

		ireg.ax = 0x4f01;
		ireg.cx = mode;
		ireg.di = (size_t) &vminfo;
		intcall(0x10, &ireg, &oreg);

		if (oreg.ax != 0x004f)
		  continue;

		/* refer to "Page 30" in:
		 * https://pdos.csail.mit.edu/6.828/2018/readings/hardware/vbe3.pdf
		 */
		if ((vminfo.mode_attr & 0x15) == 0x05) {
			/* Text Mode, TTY BIOS supported,
			 * supported by hardware */
			mi = GET_HEAP(struct mode_info, 1);
			mi->mode = mode + VIDEO_FIRST_VESA;
			mi->depth = 0;	/* text */
			mi->x = vminfo.h_res;
			mi->y = vminfo.v_res;
			nmodes++;
		} else if ((vminfo.mode_attr & 0x99) == 0x99 &&
				(vminfo.memory_layout == 4 || vminfo.memory_layout == 6) &&
				vminfo.memory_planes == 1) {
#ifdef CONFIG_FB_BOOT_VESA_SUPPORT
			/* Graphics mode, color, linear frame buffer
			 * supported.  Only register the mode if
			 * framebuffer is configured, however, otherwise
			 * the use will be left without a screen. */
			mi = GET_HEAP(struct mode_info, 1);
			mi->mode = mode + VIDEO_FIRST_VESA;
			mi->depth = vminfo.bpp;
			mi->x = vminfo.h_res;
			mi->y = vminfo.v_res;
			nmodes++;
#endif
		}
	}

	return nmodes;
}

static int vesa_set_mode(struct mode_info *mode)
{
	struct biosregs ireg, oreg;
	int is_graphic;
	u16 vesa_mode = mode->mode - VIDEO_FIRST_VESA;

	memset(&vminfo, 0, sizeof vminfo);	/* Just in case... */

	initregs(&ireg);
	ireg.ax = 0x4f01;
	ireg.cx = vesa_mode;
	ireg.di = (size_t) &vminfo;
	intcall(0x10, &ireg, &oreg);

	if (oreg.ax != 0x004f)
	  return -1;

	/* refer to: vbe3.pdf */
	if ((vminfo.mode_attr & 0x15) == 0x05) {
		/* It's a supported text mode */
		is_graphic = 0;
#ifdef CONFIG_FB_BOOT_VESA_SUPPORT
	} else if ((vminfo.mode_attr & 0x99) == 0x99) {
		/* It's a graphics mode with linear frame buffer */
		is_graphic = 1;
		vesa_mode |= 0x4000;	/* Request linear/flat frame buffer */
#endif
	} else {
		return -1;	/* Invalid mode */
	}

	initregs(&ireg);
	ireg.ax = 0x4f02;
	ireg.bx = vesa_mode;
	intcall(0x10, &ireg, &oreg);

	if (oreg.ax != 0x004f)
	  return -1;

	graphic_mode = is_graphic;
	if (! is_graphic) {
		/* Text mode */
		force_x = mode->x;
		force_y = mode->y;
		do_restore = 1;
	} else {
		/* Graphics mode */
		vesa_store_mode_params_graphics();
	}

	return 0;
}


#ifndef _WAKEUP

/* Switch DAC to 8-bit mode */
static void vesa_dac_set_8bits(void)
{
	struct biosregs ireg, oreg;
	u8 dac_size = 6;

	/* refer to: vbe3.pdf */
	/* If possible, switch the DAC to 8-bit mode */
	if (vginfo.capabilities & 1) {
		initregs(&ireg);
		ireg.ax = 0x4f08;
		ireg.bh = 0x08;
		intcall(0x10, &ireg, &oreg);
		if (oreg.ax == 0x004f)
		  dac_size = oreg.bh;
	}

	/* Set the color sizes to the DAC size, and offsets to 0 */
	boot_params.screen_info.red_size   = dac_size;
	boot_params.screen_info.green_size = dac_size;
	boot_params.screen_info.blue_size  = dac_size;
	boot_params.screen_info.rsvd_size  = dac_size;

	boot_params.screen_info.red_pos    = 0;
	boot_params.screen_info.green_pos  = 0;
	boot_params.screen_info.blue_pos   = 0;
	boot_params.screen_info.rsvd_pos   = 0;
}

/* Svae the VESA protected mode info */
static void vesa_store_pm_info(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ax = 0x4f0a;
	intcall(0x10, &ireg, &oreg);

	if (oreg.ax != 0x004f)
	  return;

	boot_params.screen_info.vesapm_seg = oreg.es;
	boot_params.screen_info.vesapm_off = oreg.di;
}

/* 
 * Save video mode parameters for graphics mode
 */
static void vesa_store_mode_params_graphics(void)
{
	/* Tell the kernel we're in VESA graphics mode */
	boot_params.screen_info.orig_video_isVGA = VIDEO_TYPE_VLFB;

	/* Mode parameters */
	boot_params.screen_info.vesa_attributes = vminfo.mode_attr;
	boot_params.screen_info.lfb_linelength = vminfo.logical_scan;
	boot_params.screen_info.lfb_width = vminfo.h_res;
	boot_params.screen_info.lfb_height = vminfo.v_res;
	boot_params.screen_info.lfb_depth = vminfo.bpp;
	boot_params.screen_info.pages = vminfo.image_planes;
	boot_params.screen_info.lfb_base = vminfo.lfb_ptr;
	memcpy(&boot_params.screen_info.red_size, &vminfo.rmask, 8);

	/* General parameters */
	boot_params.screen_info.lfb_size = vginfo.total_memory;

	if (vminfo.bpp <= 8)
	  vesa_dac_set_8bits();

	vesa_store_pm_info();
}

#endif /* ! _WAKEUP */

static __videocard video_vesa = 
{
	.card_name  = "VESA",
	.probe      = vesa_probe,
	.set_mode   = vesa_set_mode,
	.xmode_first    = VIDEO_FIRST_VESA,
	.xmode_n    = 0x200,
};
