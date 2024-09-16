#ifndef _ASM_X86_PARAVIRT_H
#define _ASM_X86_PARAVIRT_H
/* Various instructions on x86 need to be replaced for
 * para-virtualization: those hooks are defined here. */

#ifdef CONFIG_PARAVIRT
//TODO
#else  /* CONFIG_PARAVIRT */
# define default_banner x86_init_noop
#endif /* !CONFIG_PARAVIRT */
#endif /* _ASM_X86_PARAVIRT_H */
