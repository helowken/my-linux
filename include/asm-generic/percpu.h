#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_

#include <linux/compiler.h>
#include <linux/threads.h>
#include <linux/percpu-defs.h>

#ifdef CONFIG_SMP

/*
 * per_cpu_offset() is the offset that has to be added to a
 * percpu variable to get to the instance for a certain processor.
 *
 * Most arches use the __per_cpu_offset array for those offsets but
 * some arches have their own ways of determining the offset (x86_64, s390).
 */
#ifndef __per_cpu_offset
extern unsigned long __per_cpu_offset[NR_CPUS];

#define per_cpu_offset(x) (__per_cpu_offset[x])
#endif

/*
 * Determine the offset for the currently active processor.
 * An arch may define __my_cpu_offset to provide a more effective
 * means of obtaining the offset to the per cpu variables of the
 * current processor.
 */
#ifndef __my_cpu_offset
#define __my_cpu_offset per_cpu_offset(raw_smp_processor_id())
#endif
#ifdef CONFIG_DEBUG_PREEMPT
//#define my_cpu_offset per_cpu_offset(smp_processor_id())
#else
#define my_cpu_offset __my_cpu_offset
#endif

/*
 * Add a offset to a pointer but keep the pointer as is.
 *
 * Only S390 provides its own means of moving the pointer.
 */
#ifndef SHIFT_PERCPU_PTR
#define SHIFT_PERCPU_PTR(__p, __offset)	RELOC_HIDE((__p), (__offset))
#endif

/*
 * A percpu variable may point to a discarded regions. The following are
 * established ways to produce a usable pointer from the percpu variable
 * offset.
 */
#define per_cpu(var, cpu) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), per_cpu_offset(cpu)))
#define __get_cpu_var(var) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), my_cpu_offset))
#define __raw_get_cpu_var(var) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), __my_cpu_offset))


#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
extern void setup_per_cpu_areas(void);
#endif

#else /* ! SMP */
/*
#define per_cpu(var, cpu)			(*((void)(cpu), &per_cpu_var(var)))
#define __get_cpu_var(var)			per_cpu_var(var)
#define __raw_get_cpu_var(var)			per_cpu_var(var)
*/
#endif	/* SMP */

#ifndef PER_CPU_BASE_SECTION
#ifdef CONFIG_SMP
#define PER_CPU_BASE_SECTION ".data.percpu"
#else
#define PER_CPU_BASE_SECTION ".data"
#endif
#endif

#ifdef CONFIG_SMP

#ifdef MODULE
#define PER_CPU_SHARED_ALIGNED_SECTION ""
#define PER_CPU_ALIGNED_SECTION ""
#else
#define PER_CPU_SHARED_ALIGNED_SECTION ".shared_aligned"
#define PER_CPU_ALIGNED_SECTION ".shared_aligned"
#endif
#define PER_CPU_FIRST_SECTION ".first"

#else
/*
#define PER_CPU_SHARED_ALIGNED_SECTION ""
#define PER_CPU_ALIGNED_SECTION ".shared_aligned"
#define PER_CPU_FIRST_SECTION ""
*/
#endif

#ifndef PER_CPU_ATTRIBUTES
#define PER_CPU_ATTRIBUTES
#endif

#ifndef PER_CPU_DEF_ATTRIBUTES
#define PER_CPU_DEF_ATTRIBUTES
#endif

#endif /* _ASM_GENERIC_PERCPU_H_ */
