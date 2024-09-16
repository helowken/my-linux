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
 * Check for obligatory CPU features and abort if the features are not
 * present.  This code should be compilable as 16-, 32- or 64-bit
 * code, so be very careful with types and inline assembly.
 *
 * This code should not contain any messages; that requires an
 * additional wrapper.
 *
 * As written, this code is not safe for inclusion into the kernel
 * proper (after FPU initialization, in particular).
 */

#ifdef _SETUP
# include "boot.h"
#endif
#include <linux/types.h>
#include <asm/processor-flags.h>
#include <asm/required-features.h>
#include <asm/msr-index.h>

struct cpu_features cpu;
static u32 cpu_vendor[3];
static u32 err_flags[NCAPINTS];

static const int req_level = CONFIG_X86_MINIMUM_CPU_FAMILY;

static const u32 req_flags[NCAPINTS] = 
{
	REQUIRED_MASK0,
	REQUIRED_MASK1,
	0,	/* REQUIRED_MASK2 not implemented in this file */
	0,	/* REQUIRED_MASK3 not implemented in this file */
	REQUIRED_MASK4,
	0,	/* REQUIRED_MASK5 not implemented in this file */
	REQUIRED_MASK6,
	0,	/* REQUIRED_MASK7 not implemented in this file */
};

static int has_eflag(u32 mask)
{
	u32 f0, f1;

	asm("pushfl ; "
		"pushfl ; "
		"popl %0 ; "
		"movl %0,%1 ; "
		"xorl %2,%1 ; "
		"pushl %1 ; "
		"popfl ; "
		"pushfl ; "
		"popl %1 ; "
		"popfl"
		: "=&r" (f0), "=&r" (f1)
		: "ri" (mask));

	return !!((f0^f1) & mask);
}

static int has_fpu(void) 
{
	u16 fcw = -1, fsw = -1;
	u32 cr0;

	asm("movl %%cr0,%0" : "=r" (cr0));
	if (cr0 & (X86_CR0_EM | X86_CR0_TS)) {
		cr0 &= ~(X86_CR0_EM | X86_CR0_TS);
		asm volatile("movl %0,%%cr0" : : "r" (cr0));
	}

	/* fninit: Sets the FPU control, status, tag, instruction 
	 *         pointer, and data pointer registers to their 
	 *         default states.
	 * fnstsw: Stores the current value of the x87 FPU status 
	 *         word in the destination location.
	 * fnstcw: Stores the current value of the FPU control 
	 *         word at the specified destination in memory.
	 */
	asm volatile("fninit ; fnstsw %0 ; fnstcw %1"
			: "+m" (fsw), "+m" (fcw));

	/* Refer to "8.1.3" and "8.1.5" in 64-ia-32-vol-1-manual.pdf.
	 */
	return fsw == 0 && (fcw & 0x103f) == 0x003f;
}

static void get_flags(void) 
{
	u32 max_intel_level, max_amd_level;
	u32 tfms;

	if (has_fpu())
	  set_bit(X86_FEATURE_FPU, cpu.flags);

	if (has_eflag(X86_EFLAGS_ID)) {
		asm("cpuid"
			: "=a" (max_intel_level),
			  "=b" (cpu_vendor[0]),
			  "=d" (cpu_vendor[1]),
			  "=c" (cpu_vendor[2])
			: "a" (0));

		if (max_intel_level >= 0x00000001 &&
				max_intel_level <= 0x0000ffff) {
			asm("cpuid"
				: "=a" (tfms),
				  "=c" (cpu.flags[4]),
				  "=d" (cpu.flags[0])
				: "a" (0x00000001)
				: "ebx");
			cpu.level = (tfms >> 8) & 0xF;
			cpu.model = (tfms >> 4) & 0xF;
			if (cpu.level >= 6)
			  cpu.model += ((tfms >> 16) & 0xF) << 4;
		}

		asm("cpuid"
			: "=a" (max_amd_level)
			: "a" (0x80000000)
			: "ebx", "ecx", "edx");

		if (max_amd_level >= 0x80000001 &&
				max_amd_level <= 0x8000ffff) {
			u32 eax = 0x80000001;
			asm("cpuid"
				: "+a" (eax),
				  "=c" (cpu.flags[6]),
				  "=d" (cpu.flags[1])
				: : "ebx");
		}
	}
}

/* Return a bitmask of which words we have error bits in */
static int check_flags(void)
{
	u32 err;
	int i;

	err = 0;
	for (i = 0; i < NCAPINTS; i++) {
		err_flags[i] = req_flags[i] & ~cpu.flags[i];
		if (err_flags[i])
		  err |= 1 << i;
	}

	return err;
}

/*
 * Returns -1 on error.
 *
 * *cpu_level is set to the current CPU level; *req_level to the required
 * level,  x86-64 is considered level 64 for this purpose.
 *
 * *err_flags_ptr is set to the flags error array if there are flags missing.
 */
int check_cpu(int *cpu_level_ptr, int *req_level_ptr, u32 **err_flags_ptr)
{
	int err;

	memset(&cpu.flags, 0, sizeof(cpu.flags));
	cpu.level = 3;

	if (has_eflag(X86_EFLAGS_AC))
	  cpu.level = 4;

	get_flags();
	err = check_flags();

	if (test_bit(X86_FEATURE_LM, cpu.flags))
	  cpu.level = 64;

	//TODO: NO_SUPPORT for amd, centaur and transmeta
	
	if (err_flags_ptr)
	  *err_flags_ptr = err ? err_flags : NULL;
	if (cpu_level_ptr)
	  *cpu_level_ptr = cpu.level;
	if (req_level_ptr)
	  *req_level_ptr = req_level;

	return (cpu.level < req_level || err) ? -1 : 0;
}



