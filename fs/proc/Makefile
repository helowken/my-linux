#
# Makefile for the Linux proc filesystem routines.
#

#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

obj-$(CONFIG_PROC_FS) += proc.o

proc-y			:= nommu.o task_nommu.o
proc-$(CONFIG_MMU)	:= mmu.o task_mmu.o

proc-y       += inode.o root.o base.o generic.o array.o \
		proc_tty.o
proc-y	+= cmdline.o
proc-y	+= cpuinfo.o
proc-y	+= devices.o
proc-y	+= interrupts.o
proc-y	+= loadavg.o
proc-y	+= meminfo.o
proc-y	+= stat.o
proc-y	+= uptime.o
proc-y	+= version.o
proc-y	+= softirqs.o
proc-$(CONFIG_PROC_SYSCTL)	+= proc_sysctl.o
proc-$(CONFIG_NET)		+= proc_net.o
proc-$(CONFIG_PROC_KCORE)	+= kcore.o
proc-$(CONFIG_PROC_VMCORE)	+= vmcore.o
proc-$(CONFIG_PROC_DEVICETREE)	+= proc_devtree.o
proc-$(CONFIG_PRINTK)	+= kmsg.o
proc-$(CONFIG_PROC_PAGE_MONITOR)	+= page.o
