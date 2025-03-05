#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

obj-y		:= main.o if.o generic.o state.o cleanup.o
obj-$(CONFIG_X86_32) += amd.o cyrix.o centaur.o

