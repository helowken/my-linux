#
# Makefile for the linux kernel.
#
#
#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

obj-$(CONFIG_DMI)		+= dmi_scan.o
obj-$(CONFIG_EDD)		+= edd.o
obj-$(CONFIG_EFI_VARS)		+= efivars.o
obj-$(CONFIG_EFI_PCDP)		+= pcdp.o
obj-$(CONFIG_DELL_RBU)          += dell_rbu.o
obj-$(CONFIG_DCDBAS)		+= dcdbas.o
obj-$(CONFIG_DMIID)		+= dmi-id.o
obj-$(CONFIG_ISCSI_IBFT_FIND)	+= iscsi_ibft_find.o
obj-$(CONFIG_ISCSI_IBFT)	+= iscsi_ibft.o
obj-$(CONFIG_FIRMWARE_MEMMAP)	+= memmap.o
