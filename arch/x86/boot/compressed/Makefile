#
# linux/arch/x86/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

targets := vmlinux.lds vmlinux vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2 vmlinux.bin.lzma head_$(BITS).o misc.o piggy.o

#TODO KBUILD_CFLAGS := -m$(BITS) -D__KERNEL__ $(LINUX_INCLUDE) -O2
KBUILD_CFLAGS := -m$(BITS) -D__KERNEL__ $(LINUX_INCLUDE)
KBUILD_CFLAGS += -fno-strict-aliasing -fPIC
KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING
cflags-$(CONFIG_X86_64) := -mcmodel=small
KBUILD_CFLAGS += $(cflags-y)
KBUILD_CFLAGS += $(call cc-option,-ffreestanding)
KBUILD_CFLAGS += $(call cc-option,-fno-stack-protector)

KBUILD_AFLAGS  := $(KBUILD_CFLAGS) -D__ASSEMBLY__
GCOV_PROFILE := n

LDFLAGS := -m elf_$(UTS_MACHINE)
LDFLAGS_vmlinux := -T

hostprogs-y	:= mkpiggy

$(obj)/vmlinux: $(obj)/vmlinux.lds $(obj)/head_$(BITS).o $(obj)/misc.o $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

OBJCOPYFLAGS_vmlinux.bin :=  -R .comment -S
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)


targets += vmlinux.bin.all vmlinux.relocs relocs
hostprogs-$(CONFIG_X86_NEED_RELOCS) += relocs

quiet_cmd_relocs = RELOCS  $@
      cmd_relocs = $(obj)/relocs $< > $@;$(obj)/relocs --abs-relocs $<
$(obj)/vmlinux.relocs: vmlinux $(obj)/relocs FORCE
	$(call if_changed,relocs)

vmlinux.bin.all-y := $(obj)/vmlinux.bin
vmlinux.bin.all-$(CONFIG_X86_NEED_RELOCS) += $(obj)/vmlinux.relocs

$(obj)/vmlinux.bin.gz: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,gzip)
$(obj)/vmlinux.bin.bz2: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,bzip2)
$(obj)/vmlinux.bin.lzma: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lzma)

suffix-$(CONFIG_KERNEL_GZIP)	:= gz
suffix-$(CONFIG_KERNEL_BZIP2)	:= bz2
suffix-$(CONFIG_KERNEL_LZMA)	:= lzma

quiet_cmd_mkpiggy = MKPIGGY $@
      cmd_mkpiggy = $(obj)/mkpiggy $< > $@ || ( rm -f $@ ; false )

targets += piggy.S
$(obj)/piggy.S: $(obj)/vmlinux.bin.$(suffix-y) $(obj)/mkpiggy FORCE
	$(call if_changed,mkpiggy)
