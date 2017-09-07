# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

SRC_DIR := third_party/lib/acpica/source

# We currently only support ACPICA on x86, so compile to an empty
# lib if we're on non-x86.
ifeq ($(ARCH),x86)
MODULE_SRCS += \
	$(SRC_DIR)/components/hardware/hwacpi.c \
	$(SRC_DIR)/components/hardware/hwxfsleep.c \
	$(SRC_DIR)/components/hardware/hwgpe.c \
	$(SRC_DIR)/components/hardware/hwxface.c \
	$(SRC_DIR)/components/hardware/hwregs.c \
	$(SRC_DIR)/components/hardware/hwpci.c \
	$(SRC_DIR)/components/hardware/hwvalid.c \
	$(SRC_DIR)/components/hardware/hwtimer.c \
	$(SRC_DIR)/components/hardware/hwesleep.c \
	$(SRC_DIR)/components/hardware/hwsleep.c \
	$(SRC_DIR)/components/executer/exstore.c \
	$(SRC_DIR)/components/executer/exoparg3.c \
	$(SRC_DIR)/components/executer/exutils.c \
	$(SRC_DIR)/components/executer/exfield.c \
	$(SRC_DIR)/components/executer/exnames.c \
	$(SRC_DIR)/components/executer/exstoren.c \
	$(SRC_DIR)/components/executer/exstorob.c \
	$(SRC_DIR)/components/executer/exresop.c \
	$(SRC_DIR)/components/executer/exfldio.c \
	$(SRC_DIR)/components/executer/exmutex.c \
	$(SRC_DIR)/components/executer/exdump.c \
	$(SRC_DIR)/components/executer/exoparg2.c \
	$(SRC_DIR)/components/executer/exsystem.c \
	$(SRC_DIR)/components/executer/exmisc.c \
	$(SRC_DIR)/components/executer/exoparg6.c \
	$(SRC_DIR)/components/executer/excreate.c \
	$(SRC_DIR)/components/executer/exresnte.c \
	$(SRC_DIR)/components/executer/exconfig.c \
	$(SRC_DIR)/components/executer/exresolv.c \
	$(SRC_DIR)/components/executer/exregion.c \
	$(SRC_DIR)/components/executer/exconvrt.c \
	$(SRC_DIR)/components/executer/exprep.c \
	$(SRC_DIR)/components/executer/exdebug.c \
	$(SRC_DIR)/components/executer/extrace.c \
	$(SRC_DIR)/components/executer/exoparg1.c \
	$(SRC_DIR)/components/namespace/nsnames.c \
	$(SRC_DIR)/components/namespace/nspredef.c \
	$(SRC_DIR)/components/namespace/nsaccess.c \
	$(SRC_DIR)/components/namespace/nsrepair2.c \
	$(SRC_DIR)/components/namespace/nsprepkg.c \
	$(SRC_DIR)/components/namespace/nswalk.c \
	$(SRC_DIR)/components/namespace/nsxfeval.c \
	$(SRC_DIR)/components/namespace/nsconvert.c \
	$(SRC_DIR)/components/namespace/nsdumpdv.c \
	$(SRC_DIR)/components/namespace/nsparse.c \
	$(SRC_DIR)/components/namespace/nsxfname.c \
	$(SRC_DIR)/components/namespace/nsdump.c \
	$(SRC_DIR)/components/namespace/nsarguments.c \
	$(SRC_DIR)/components/namespace/nssearch.c \
	$(SRC_DIR)/components/namespace/nsalloc.c \
	$(SRC_DIR)/components/namespace/nsutils.c \
	$(SRC_DIR)/components/namespace/nsinit.c \
	$(SRC_DIR)/components/namespace/nsobject.c \
	$(SRC_DIR)/components/namespace/nsload.c \
	$(SRC_DIR)/components/namespace/nseval.c \
	$(SRC_DIR)/components/namespace/nsrepair.c \
	$(SRC_DIR)/components/namespace/nsxfobj.c \
	$(SRC_DIR)/components/resources/rsaddr.c \
	$(SRC_DIR)/components/resources/rscalc.c \
	$(SRC_DIR)/components/resources/rsserial.c \
	$(SRC_DIR)/components/resources/rscreate.c \
	$(SRC_DIR)/components/resources/rsutils.c \
	$(SRC_DIR)/components/resources/rslist.c \
	$(SRC_DIR)/components/resources/rsmemory.c \
	$(SRC_DIR)/components/resources/rsinfo.c \
	$(SRC_DIR)/components/resources/rsxface.c \
	$(SRC_DIR)/components/resources/rsmisc.c \
	$(SRC_DIR)/components/resources/rsio.c \
	$(SRC_DIR)/components/resources/rsdumpinfo.c \
	$(SRC_DIR)/components/resources/rsirq.c \
	$(SRC_DIR)/components/tables/tbinstal.c \
	$(SRC_DIR)/components/tables/tbprint.c \
	$(SRC_DIR)/components/tables/tbfadt.c \
	$(SRC_DIR)/components/tables/tbxfroot.c \
	$(SRC_DIR)/components/tables/tbfind.c \
	$(SRC_DIR)/components/tables/tbxface.c \
	$(SRC_DIR)/components/tables/tbxfload.c \
	$(SRC_DIR)/components/tables/tbdata.c \
	$(SRC_DIR)/components/tables/tbutils.c \
	$(SRC_DIR)/components/parser/psloop.c \
	$(SRC_DIR)/components/parser/psutils.c \
	$(SRC_DIR)/components/parser/pstree.c \
	$(SRC_DIR)/components/parser/pswalk.c \
	$(SRC_DIR)/components/parser/psargs.c \
	$(SRC_DIR)/components/parser/psopinfo.c \
	$(SRC_DIR)/components/parser/psxface.c \
	$(SRC_DIR)/components/parser/psscope.c \
	$(SRC_DIR)/components/parser/psobject.c \
	$(SRC_DIR)/components/parser/psparse.c \
	$(SRC_DIR)/components/parser/psopcode.c \
	$(SRC_DIR)/components/dispatcher/dsdebug.c \
	$(SRC_DIR)/components/dispatcher/dsobject.c \
	$(SRC_DIR)/components/dispatcher/dswexec.c \
	$(SRC_DIR)/components/dispatcher/dswscope.c \
	$(SRC_DIR)/components/dispatcher/dsinit.c \
	$(SRC_DIR)/components/dispatcher/dsutils.c \
	$(SRC_DIR)/components/dispatcher/dswstate.c \
	$(SRC_DIR)/components/dispatcher/dswload2.c \
	$(SRC_DIR)/components/dispatcher/dsfield.c \
	$(SRC_DIR)/components/dispatcher/dsmthdat.c \
	$(SRC_DIR)/components/dispatcher/dscontrol.c \
	$(SRC_DIR)/components/dispatcher/dswload.c \
	$(SRC_DIR)/components/dispatcher/dsopcode.c \
	$(SRC_DIR)/components/dispatcher/dsargs.c \
	$(SRC_DIR)/components/dispatcher/dsmethod.c \
	$(SRC_DIR)/components/events/evxfevnt.c \
	$(SRC_DIR)/components/events/evgpeblk.c \
	$(SRC_DIR)/components/events/evgpe.c \
	$(SRC_DIR)/components/events/evxfgpe.c \
	$(SRC_DIR)/components/events/evrgnini.c \
	$(SRC_DIR)/components/events/evgpeutil.c \
	$(SRC_DIR)/components/events/evglock.c \
	$(SRC_DIR)/components/events/evregion.c \
	$(SRC_DIR)/components/events/evxfregn.c \
	$(SRC_DIR)/components/events/evevent.c \
	$(SRC_DIR)/components/events/evsci.c \
	$(SRC_DIR)/components/events/evgpeinit.c \
	$(SRC_DIR)/components/events/evhandler.c \
	$(SRC_DIR)/components/events/evmisc.c \
	$(SRC_DIR)/components/events/evxface.c \
	$(SRC_DIR)/components/utilities/utxferror.c \
	$(SRC_DIR)/components/utilities/utxfmutex.c \
	$(SRC_DIR)/components/utilities/utmisc.c \
	$(SRC_DIR)/components/utilities/utmutex.c \
	$(SRC_DIR)/components/utilities/utbuffer.c \
	$(SRC_DIR)/components/utilities/utobject.c \
	$(SRC_DIR)/components/utilities/uterror.c \
	$(SRC_DIR)/components/utilities/utstring.c \
	$(SRC_DIR)/components/utilities/utmath.c \
	$(SRC_DIR)/components/utilities/utpredef.c \
	$(SRC_DIR)/components/utilities/utprint.c \
	$(SRC_DIR)/components/utilities/utdecode.c \
	$(SRC_DIR)/components/utilities/utosi.c \
	$(SRC_DIR)/components/utilities/utdebug.c \
	$(SRC_DIR)/components/utilities/utaddress.c \
	$(SRC_DIR)/components/utilities/utuuid.c \
	$(SRC_DIR)/components/utilities/utcache.c \
	$(SRC_DIR)/components/utilities/utexcep.c \
	$(SRC_DIR)/components/utilities/uttrack.c \
	$(SRC_DIR)/components/utilities/uthex.c \
	$(SRC_DIR)/components/utilities/uteval.c \
	$(SRC_DIR)/components/utilities/utxface.c \
	$(SRC_DIR)/components/utilities/utownerid.c \
	$(SRC_DIR)/components/utilities/utstate.c \
	$(SRC_DIR)/components/utilities/utlock.c \
	$(SRC_DIR)/components/utilities/utnonansi.c \
	$(SRC_DIR)/components/utilities/utdelete.c \
	$(SRC_DIR)/components/utilities/utresrc.c \
	$(SRC_DIR)/components/utilities/utcopy.c \
	$(SRC_DIR)/components/utilities/utalloc.c \
	$(SRC_DIR)/components/utilities/utxfinit.c \
	$(SRC_DIR)/components/utilities/utglobal.c \
	$(SRC_DIR)/components/utilities/utinit.c \
	$(SRC_DIR)/components/utilities/utclib.c \
	$(SRC_DIR)/components/utilities/utids.c \
	$(SRC_DIR)/common/getopt.c \
	$(SRC_DIR)/common/ahpredef.c \
	$(SRC_DIR)/common/ahids.c \
	$(SRC_DIR)/common/ahtable.c \
	$(SRC_DIR)/os_specific/service_layers/osfuchsia.cpp
else
MODULE_SRCS += $(LOCAL_DIR)/empty.c
endif

# Disable these two warnings to prevent ACPICA from cluttering the
# build output
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
MODULE_CFLAGS += -Wno-discarded-qualifiers
else
MODULE_CFLAGS += -Wno-incompatible-pointer-types-discards-qualifiers
endif
# We need to specify -fno-strict-aliasing, since ACPICA has a habit of violating strict aliasing
# rules in some of its macros.  Rewriting this code would increase the maintenance cost of
# bringing in the latest upstream ACPICA, so instead we mitigate the problem with a compile-time
# flag.  We take the more conservative approach of disabling strict-aliasing-based optimizations,
# rather than disabling warnings.
MODULE_CFLAGS += -fno-strict-aliasing

MODULE_COMPILEFLAGS += -I$(SRC_DIR)/include/acpica

ifeq ($(call TOBOOL,$(ENABLE_ACPI_DEBUG)),true)
MODULE_COMPILEFLAGS += -DACPI_DEBUG_OUTPUT
endif

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/mxcpp \

MODULE_LIBS := \
    system/ulib/c

include make/module.mk
