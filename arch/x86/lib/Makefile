#
# Makefile for x86 specific library files.
#

#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

obj-$(CONFIG_SMP) += msr-smp.o cache-smp.o

lib-y := delay.o
lib-y += thunk_$(BITS).o
lib-y += usercopy_$(BITS).o getuser.o putuser.o
lib-y += memcpy_$(BITS).o

obj-y += msr.o msr-reg.o msr-reg-export.o

ifeq ($(CONFIG_X86_32),y)
        obj-y += atomic64_32.o
        lib-y += checksum_32.o
        lib-y += strstr_32.o
        lib-y += semaphore_32.o string_32.o
ifneq ($(CONFIG_X86_CMPXCHG64),y)
        lib-y += cmpxchg8b_emu.o
endif
        lib-$(CONFIG_X86_USE_3DNOW) += mmx_32.o
else
        obj-y += io_64.o iomap_copy_64.o
        lib-y += csum-partial_64.o csum-copy_64.o csum-wrappers_64.o
        lib-y += thunk_64.o clear_page_64.o copy_page_64.o
        lib-y += memmove_64.o memset_64.o
        lib-y += copy_user_64.o rwlock_64.o copy_user_nocache_64.o
	lib-$(CONFIG_RWSEM_XCHGADD_ALGORITHM) += rwsem_64.o
endif
