#
# Makefile for the Linux IEEE 1394 implementation
#

#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

firewire-core-y += core-card.o core-cdev.o core-device.o \
                   core-iso.o core-topology.o core-transaction.o
firewire-ohci-y += ohci.o
firewire-sbp2-y += sbp2.o
firewire-net-y  += net.o

obj-$(CONFIG_FIREWIRE)      += firewire-core.o
obj-$(CONFIG_FIREWIRE_OHCI) += firewire-ohci.o
obj-$(CONFIG_FIREWIRE_SBP2) += firewire-sbp2.o
obj-$(CONFIG_FIREWIRE_NET)  += firewire-net.o
