# Makefile for Linux PHY drivers

#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

libphy-objs			:= phy.o phy_device.o mdio_bus.o

obj-$(CONFIG_PHYLIB)		+= libphy.o
obj-$(CONFIG_MARVELL_PHY)	+= marvell.o
obj-$(CONFIG_DAVICOM_PHY)	+= davicom.o
obj-$(CONFIG_CICADA_PHY)	+= cicada.o
obj-$(CONFIG_LXT_PHY)		+= lxt.o
obj-$(CONFIG_QSEMI_PHY)		+= qsemi.o
obj-$(CONFIG_SMSC_PHY)		+= smsc.o
obj-$(CONFIG_VITESSE_PHY)	+= vitesse.o
obj-$(CONFIG_BROADCOM_PHY)	+= broadcom.o
obj-$(CONFIG_BCM63XX_PHY)	+= bcm63xx.o
obj-$(CONFIG_ICPLUS_PHY)	+= icplus.o
obj-$(CONFIG_REALTEK_PHY)	+= realtek.o
obj-$(CONFIG_LSI_ET1011C_PHY)	+= et1011c.o
obj-$(CONFIG_FIXED_PHY)		+= fixed.o
obj-$(CONFIG_MDIO_BITBANG)	+= mdio-bitbang.o
obj-$(CONFIG_MDIO_GPIO)		+= mdio-gpio.o
obj-$(CONFIG_NATIONAL_PHY)	+= national.o
obj-$(CONFIG_STE10XP)		+= ste10Xp.o
