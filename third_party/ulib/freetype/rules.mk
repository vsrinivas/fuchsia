LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
	$(LOCAL_DIR)/src/base/ftsystem.c \
	$(LOCAL_DIR)/src/base/ftinit.c \
	$(LOCAL_DIR)/src/base/ftdebug.c \
	$(LOCAL_DIR)/src/base/ftbase.c \
	$(LOCAL_DIR)/src/base/ftbbox.c \
	$(LOCAL_DIR)/src/base/ftglyph.c \
	$(LOCAL_DIR)/src/base/ftbdf.c \
	$(LOCAL_DIR)/src/base/ftbitmap.c \
	$(LOCAL_DIR)/src/base/ftcid.c \
	$(LOCAL_DIR)/src/base/ftfntfmt.c \
	$(LOCAL_DIR)/src/base/ftfstype.c \
	$(LOCAL_DIR)/src/base/ftgasp.c \
	$(LOCAL_DIR)/src/base/ftgxval.c \
	$(LOCAL_DIR)/src/base/ftlcdfil.c \
	$(LOCAL_DIR)/src/base/ftmm.c \
	$(LOCAL_DIR)/src/base/ftotval.c \
	$(LOCAL_DIR)/src/base/ftpatent.c \
	$(LOCAL_DIR)/src/base/ftpfr.c \
	$(LOCAL_DIR)/src/base/ftstroke.c \
	$(LOCAL_DIR)/src/base/ftsynth.c \
	$(LOCAL_DIR)/src/base/fttype1.c \
	$(LOCAL_DIR)/src/base/ftwinfnt.c \
	$(LOCAL_DIR)/src/bdf/bdf.c \
	$(LOCAL_DIR)/src/cff/cff.c \
	$(LOCAL_DIR)/src/cid/type1cid.c \
	$(LOCAL_DIR)/src/pcf/pcf.c \
	$(LOCAL_DIR)/src/pfr/pfr.c \
	$(LOCAL_DIR)/src/sfnt/sfnt.c \
	$(LOCAL_DIR)/src/truetype/truetype.c \
	$(LOCAL_DIR)/src/type1/type1.c \
	$(LOCAL_DIR)/src/type42/type42.c \
	$(LOCAL_DIR)/src/winfonts/winfnt.c \
	$(LOCAL_DIR)/src/raster/raster.c \
	$(LOCAL_DIR)/src/smooth/smooth.c \
	$(LOCAL_DIR)/src/autofit/autofit.c \
	$(LOCAL_DIR)/src/cache/ftcache.c \
	$(LOCAL_DIR)/src/gzip/ftgzip.c \
	$(LOCAL_DIR)/src/lzw/ftlzw.c \
	$(LOCAL_DIR)/src/bzip2/ftbzip2.c \
	$(LOCAL_DIR)/src/gxvalid/gxvalid.c \
	$(LOCAL_DIR)/src/otvalid/otvalid.c \
	$(LOCAL_DIR)/src/psaux/psaux.c \
	$(LOCAL_DIR)/src/pshinter/pshinter.c \
	$(LOCAL_DIR)/src/psnames/psnames.c \
#

MODULE_DEPS := \
    external/ulib/musl

MODULE_CFLAGS += \
    -I$(LOCAL_DIR)" \
    -DFT_CONFIG_CONFIG_H=\"builds/magenta/ftconfig.h\"" \
    -DFT2_BUILD_LIBRARY \
    "-DFT_CONFIG_MODULES_H=\"freetype/config/ftmodule.h\"" \
    -Wno-error \
#

USER_MANIFEST_LINES += fonts/Inconsolata-Regular.ttf=$(LOCAL_DIR)/Inconsolata-Regular.ttf

include make/module.mk
