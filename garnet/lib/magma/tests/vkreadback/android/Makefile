CLANG=$(HOME)/projects/ndk-toolchain/bin/clang++
MAGMA_ROOT=../../..
SRCDIR=$(MAGMA_ROOT)/tests/vkreadback
INCLUDES=-I$(MAGMA_ROOT)/src
DEFINES=-DMAGMA_DEBUG_INTERNAL_USE_ONLY=1
LIBS=-lvulkan

vkreadback: $(SRCDIR)/vkreadback.cc
	$(CLANG) -fPIE -pie -static-libstdc++ -std=c++11 $(SRCDIR)/vkreadback.cc -o vkreadback $(INCLUDES) $(DEFINES) $(LIBS)

install: vkreadback
	adb push vkreadback /data
