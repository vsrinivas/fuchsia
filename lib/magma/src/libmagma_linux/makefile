# TODO(TC-265): support linux as a target option for vm-destined binaries

TARGETNAME := magma_test

FUCHSIA_DIR ?= ../../../../..
INTDIR := /tmp/virtio_magma/$(TARGETNAME)
OUTDIR := $(FUCHSIA_DIR)/garnet/bin/guest/pkg/biscotti_guest/images/x64/sysroot/usr/bin
TARGET := $(OUTDIR)/$(TARGETNAME)
LIBS := dl
INCLUDE_DIRS := \
	$(FUCHSIA_DIR)/garnet/lib/magma/include/magma_abi \
	$(FUCHSIA_DIR)/garnet/lib/magma/include/virtio \
	$(FUCHSIA_DIR)/garnet/lib/magma/src \
	$(FUCHSIA_DIR)/third_party/mesa/include/vulkan \
	$(LINUX_HEADERS_DIR)

SRC_EXT := cc
SRCS := $(notdir $(shell find -name '*.$(SRC_EXT)'))
OBJS := $(patsubst %.$(SRC_EXT),$(INTDIR)/%.$(SRC_EXT).o,$(SRCS))
LINKARGS := $(patsubst %,-l %,$(LIBS))
INCLUDE_ARGS := $(patsubst %,-I %,$(INCLUDE_DIRS))

CXX := clang++
CXXFLAGS := -std=c++14 -g -O0 -Wall
CXXTARGET := x86_64-pc-linux-none
CXXDEFS := -DMAGMA_DEBUG_INTERNAL_USE_ONLY=1 -DPAGE_SIZE=4096
CXXCMD := $(CXX) --target=$(CXXTARGET) $(CXXFLAGS) $(CXXDEFS)
LINKFLAGS := -static-libgcc -static-libstdc++

$(INTDIR)/%.o: %
	@echo Compile: $<
	@mkdir -p $(dir $@)
	@$(CXXCMD) -c $< $(INCLUDE_ARGS) -o $@

$(TARGET): $(OBJS)
	@echo Link: $(notdir $@)
	@mkdir -p $(dir $@)
	@$(CXXCMD) $^ $(LINKARGS) $(LINKFLAGS) -o $@

all: check-env $(TARGET)

check-env:
ifndef LINUX_HEADERS_DIR
  $(error LINUX_HEADERS_DIR must be set to the target OS kernel header path (e.g. biscotti/usr/include))
endif

.PHONY: clean
clean: check-env
	@$(RM) -r $(INTDIR)
	@$(RM) -r $(OUTDIR)
