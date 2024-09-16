# Makefile for the Linux sound card driver
#

obj-$(CONFIG_SOUND) += soundcore.o
#TODO obj-$(CONFIG_SOUND_PRIME) += sound_firmware.o
#TODO obj-$(CONFIG_SOUND_PRIME) += oss/
#TODO obj-$(CONFIG_DMASOUND) += oss/
#TODO obj-$(CONFIG_SND) += core/ i2c/ drivers/ isa/ pci/ ppc/ arm/ sh/ synth/ usb/ \
#TODO 	sparc/ spi/ parisc/ pcmcia/ mips/ soc/ atmel/
#TODO obj-$(CONFIG_SND_AOA) += aoa/
#TODO 
#TODO # This one must be compilable even if sound is configured out
#TODO obj-$(CONFIG_AC97_BUS) += ac97_bus.o
#TODO 
#TODO ifeq ($(CONFIG_SND),y)
#TODO   obj-y += last.o
#TODO endif

soundcore-objs  := sound_core.o
