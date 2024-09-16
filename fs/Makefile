#
# Makefile for the Linux filesystems.
#
# 14 Sep 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
# 

#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

obj-y :=	open.o read_write.o file_table.o super.o \
		char_dev.o stat.o exec.o pipe.o namei.o fcntl.o \
		ioctl.o readdir.o select.o fifo.o dcache.o inode.o \
		attr.o bad_inode.o file.o filesystems.o namespace.o \
		seq_file.o xattr.o libfs.o fs-writeback.o \
		pnode.o drop_caches.o splice.o sync.o utimes.o \
		stack.o fs_struct.o

ifeq ($(CONFIG_BLOCK),y)
obj-y +=	buffer.o bio.o block_dev.o direct-io.o mpage.o ioprio.o
else
obj-y +=	no-block.o
endif

obj-$(CONFIG_BLK_DEV_INTEGRITY) += bio-integrity.o
obj-y				+= notify/
obj-$(CONFIG_EPOLL)		+= eventpoll.o
obj-$(CONFIG_ANON_INODES)	+= anon_inodes.o
obj-$(CONFIG_SIGNALFD)		+= signalfd.o
obj-$(CONFIG_TIMERFD)		+= timerfd.o
obj-$(CONFIG_EVENTFD)		+= eventfd.o
obj-$(CONFIG_AIO)               += aio.o
obj-$(CONFIG_FILE_LOCKING)      += locks.o
obj-$(CONFIG_COMPAT)		+= compat.o compat_ioctl.o

nfsd-$(CONFIG_NFSD)		:= nfsctl.o
obj-y				+= $(nfsd-y) $(nfsd-m)

obj-$(CONFIG_BINFMT_AOUT)	+= binfmt_aout.o
obj-$(CONFIG_BINFMT_EM86)	+= binfmt_em86.o
obj-$(CONFIG_BINFMT_MISC)	+= binfmt_misc.o

# binfmt_script is always there
obj-y				+= binfmt_script.o

obj-$(CONFIG_BINFMT_ELF)	+= binfmt_elf.o
obj-$(CONFIG_COMPAT_BINFMT_ELF)	+= compat_binfmt_elf.o
obj-$(CONFIG_BINFMT_ELF_FDPIC)	+= binfmt_elf_fdpic.o
obj-$(CONFIG_BINFMT_SOM)	+= binfmt_som.o
obj-$(CONFIG_BINFMT_FLAT)	+= binfmt_flat.o

obj-$(CONFIG_FS_MBCACHE)	+= mbcache.o
obj-$(CONFIG_FS_POSIX_ACL)	+= posix_acl.o xattr_acl.o
#TODO obj-$(CONFIG_NFS_COMMON)	+= nfs_common/
obj-$(CONFIG_GENERIC_ACL)	+= generic_acl.o

obj-y				+= quota/

obj-$(CONFIG_PROC_FS)		+= proc/
obj-y				+= partitions/
obj-$(CONFIG_SYSFS)		+= sysfs/
obj-$(CONFIG_CONFIGFS_FS)	+= configfs/
obj-y				+= devpts/

obj-$(CONFIG_PROFILING)		+= dcookies.o
#TODO obj-$(CONFIG_DLM)		+= dlm/
 
# Do not add any filesystems before this line
obj-$(CONFIG_FSCACHE)		+= fscache/
#TODO obj-$(CONFIG_REISERFS_FS)	+= reiserfs/
obj-$(CONFIG_EXT3_FS)		+= ext3/ # Before ext2 so root fs can be ext3
obj-$(CONFIG_EXT2_FS)		+= ext2/
# We place ext4 after ext2 so plain ext2 root fs's are mounted using ext2
# unless explicitly requested by rootfstype
obj-$(CONFIG_EXT4_FS)		+= ext4/
obj-$(CONFIG_JBD)		+= jbd/
obj-$(CONFIG_JBD2)		+= jbd2/
#TODO obj-$(CONFIG_CRAMFS)		+= cramfs/
#TODO obj-$(CONFIG_SQUASHFS)		+= squashfs/
obj-y				+= ramfs/
obj-$(CONFIG_HUGETLBFS)		+= hugetlbfs/
#TODO obj-$(CONFIG_CODA_FS)		+= coda/
#TODO obj-$(CONFIG_MINIX_FS)		+= minix/
#TODO obj-$(CONFIG_FAT_FS)		+= fat/
#TODO obj-$(CONFIG_BFS_FS)		+= bfs/
obj-$(CONFIG_ISO9660_FS)	+= isofs/
#TODO obj-$(CONFIG_HFSPLUS_FS)	+= hfsplus/ # Before hfs to find wrapped HFS+
#TODO obj-$(CONFIG_HFS_FS)		+= hfs/
#TODO obj-$(CONFIG_ECRYPT_FS)		+= ecryptfs/
#TODO obj-$(CONFIG_VXFS_FS)		+= freevxfs/
#TODO obj-$(CONFIG_NFS_FS)		+= nfs/
#TODO obj-$(CONFIG_EXPORTFS)		+= exportfs/
#TODO obj-$(CONFIG_NFSD)		+= nfsd/
#TODO obj-$(CONFIG_LOCKD)		+= lockd/
obj-$(CONFIG_NLS)		+= nls/
#TODO obj-$(CONFIG_SYSV_FS)		+= sysv/
#TODO obj-$(CONFIG_SMB_FS)		+= smbfs/
#TODO obj-$(CONFIG_CIFS)		+= cifs/
#TODO obj-$(CONFIG_NCP_FS)		+= ncpfs/
#TODO obj-$(CONFIG_HPFS_FS)		+= hpfs/
#TODO obj-$(CONFIG_NTFS_FS)		+= ntfs/
#TODO obj-$(CONFIG_UFS_FS)		+= ufs/
#TODO obj-$(CONFIG_EFS_FS)		+= efs/
#TODO obj-$(CONFIG_JFFS2_FS)		+= jffs2/
#TODO obj-$(CONFIG_UBIFS_FS)		+= ubifs/
#TODO obj-$(CONFIG_AFFS_FS)		+= affs/
#TODO obj-$(CONFIG_ROMFS_FS)		+= romfs/
#TODO obj-$(CONFIG_QNX4FS_FS)		+= qnx4/
obj-$(CONFIG_AUTOFS_FS)		+= autofs/
obj-$(CONFIG_AUTOFS4_FS)	+= autofs4/
#TODO obj-$(CONFIG_ADFS_FS)		+= adfs/
obj-$(CONFIG_FUSE_FS)		+= fuse/
#TODO obj-$(CONFIG_UDF_FS)		+= udf/
#TODO obj-$(CONFIG_SUN_OPENPROMFS)	+= openpromfs/
#TODO obj-$(CONFIG_OMFS_FS)		+= omfs/
#TODO obj-$(CONFIG_JFS_FS)		+= jfs/
#TODO obj-$(CONFIG_XFS_FS)		+= xfs/
#TODO obj-$(CONFIG_9P_FS)		+= 9p/
#TODO obj-$(CONFIG_AFS_FS)		+= afs/
#TODO obj-$(CONFIG_NILFS2_FS)		+= nilfs2/
#TODO obj-$(CONFIG_BEFS_FS)		+= befs/
#TODO obj-$(CONFIG_HOSTFS)		+= hostfs/
#TODO obj-$(CONFIG_HPPFS)		+= hppfs/
#TODO obj-$(CONFIG_CACHEFILES)	+= cachefiles/
obj-$(CONFIG_DEBUG_FS)		+= debugfs/
#TODO obj-$(CONFIG_OCFS2_FS)		+= ocfs2/
#TODO obj-$(CONFIG_BTRFS_FS)		+= btrfs/
#TODO obj-$(CONFIG_GFS2_FS)           += gfs2/
#TODO obj-$(CONFIG_EXOFS_FS)          += exofs/
