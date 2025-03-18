#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

obj-$(CONFIG_INOTIFY)		+= inotify.o
obj-$(CONFIG_INOTIFY_USER)	+= inotify_fsnotify.o inotify_user.o
