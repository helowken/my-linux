#
# Makefile for ACPICA Core interpreter
#

#TODO remove -O2
KBUILD_CFLAGS := $(filter-out -O2,$(KBUILD_CFLAGS))

#TODO ccflags-y			:= -Os
ccflags-y			:= 
ccflags-$(CONFIG_ACPI_DEBUG)	+= -DACPI_DEBUG_OUTPUT

# use acpi.o to put all files here into acpi.o modparam namespace
obj-y	+= acpi.o

acpi-y := dsfield.o   dsmthdat.o  dsopcode.o  dswexec.o  dswscope.o \
	 dsmethod.o  dsobject.o  dsutils.o   dswload.o  dswstate.o \
	 dsinit.o

acpi-y += evevent.o  evregion.o  evsci.o    evxfevnt.o \
	 evmisc.o   evrgnini.o  evxface.o  evxfregn.o \
	 evgpe.o    evgpeblk.o

acpi-y += exconfig.o  exfield.o  exnames.o   exoparg6.o  exresolv.o  exstorob.o\
	 exconvrt.o  exfldio.o  exoparg1.o  exprep.o    exresop.o   exsystem.o\
	 excreate.o  exmisc.o   exoparg2.o  exregion.o  exstore.o   exutils.o \
	 exdump.o    exmutex.o  exoparg3.o  exresnte.o  exstoren.o

acpi-y += hwacpi.o  hwgpe.o  hwregs.o  hwsleep.o hwxface.o hwvalid.o

acpi-$(ACPI_FUTURE_USAGE) += hwtimer.o

acpi-y += nsaccess.o  nsload.o    nssearch.o  nsxfeval.o \
	 nsalloc.o   nseval.o    nsnames.o   nsutils.o   nsxfname.o \
	 nsdump.o    nsinit.o    nsobject.o  nswalk.o    nsxfobj.o  \
	 nsparse.o   nspredef.o  nsrepair.o

acpi-$(ACPI_FUTURE_USAGE) += nsdumpdv.o

acpi-y += psargs.o    psparse.o  psloop.o pstree.o   pswalk.o  \
	 psopcode.o  psscope.o  psutils.o  psxface.o

acpi-y += rsaddr.o rscreate.o rsinfo.o rsio.o rslist.o rsmisc.o rsxface.o \
	 rscalc.o  rsirq.o  rsmemory.o  rsutils.o

acpi-$(ACPI_FUTURE_USAGE) += rsdump.o

acpi-y += tbxface.o tbinstal.o tbutils.o tbfind.o tbfadt.o tbxfroot.o

acpi-y += utalloc.o utdebug.o uteval.o utinit.o utmisc.o utxface.o \
		utcopy.o utdelete.o utglobal.o utmath.o utobject.o \
		utstate.o utmutex.o utobject.o utresrc.o utlock.o utids.o
