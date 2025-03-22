#
# Makefile for the Linux Plug-and-Play Support.
#

#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

obj-y		:= core.o card.o driver.o resource.o manager.o support.o interface.o quirks.o

obj-$(CONFIG_PNPACPI)		+= pnpacpi/
obj-$(CONFIG_PNPBIOS)		+= pnpbios/
obj-$(CONFIG_ISAPNP)		+= isapnp/

# pnp_system_init goes after pnpacpi/pnpbios init
obj-y				+= system.o
