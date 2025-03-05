#
# Makefile for cpuidle.
#

#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

obj-y += cpuidle.o driver.o governor.o sysfs.o governors/
