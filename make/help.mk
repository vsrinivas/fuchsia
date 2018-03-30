# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# routines and rules to print some helpful stuff


#$(warning MAKECMDGOALS = $(MAKECMDGOALS))

# print some help and exit
ifeq ($(firstword $(MAKECMDGOALS)),help)
do-nothing=1

.PHONY: help
help:
	@echo "$(LKNAME) build system quick help"
	@echo "Individual projects are built into a build-<project> directory"
	@echo "Output binary is located at build-<project>/$(LKNAME).bin"
	@echo "Environment or command line variables controlling build:"
	@echo "PROJECT = <project name>"
	@echo "TOOLCHAIN_PREFIX = <absolute path to toolchain or relative path with prefix>"
	@echo "EXTERNAL_DEFINES = <additional defines to add to GLOBAL_DEFINES>"
	@echo "EXTERNAL_KERNEL_DEFINES = <additional defines to add to the kernel build>"
	@echo "EXTERNAL_MODULES = <additional modules to include in the project build>"
	@echo "HOST_TARGET = <host target to build the host tools for>"
	@echo ""
	@echo "These variables may be also placed in a file called local.mk in the"
	@echo "root of the zircon build system. local.mk is sourced by the build system"
	@echo "if present."
	@echo ""
	@echo "Special make targets:"
	@echo "make help: This help"
	@echo "make list: List of buildable projects"
	@echo "make clean: cleans build of current project"
	@echo "make spotless: removes all build directories"
	@echo "make gigaboot: build the x86 UEFI bootloader"
	@echo "make bootloader: build the project's bootloader"
	@echo "make kernel: build the kernel"
	@echo "make sysroot: build and populate the sysroot"
	@echo "make tools: build all the host tools"
	@echo "make <project>: try to build project named <project>"
	@echo ""
	@echo "Examples:"
	@echo "PROJECT=testproject make"
	@echo "PROJECT=testproject make clean"
	@echo "make testproject"
	@echo "make testproject clean"
	@echo ""
	@echo "output will be in build-testproject/"

endif

# list projects
ifeq ($(firstword $(MAKECMDGOALS)),list)
do-nothing=1

# get a list of all the .mk files in the top level project directories
PROJECTS:=$(basename $(strip $(foreach d,kernel,$(wildcard $(d)/project/*.mk))))
PROJECTS:=$(shell basename -a $(PROJECTS))

.PHONY: list
list:
	@echo 'List of all buildable projects: (look in project/ directory)'; \
	for p in $(PROJECTS); do \
		echo $$p; \
	done

endif

# vim: set syntax=make noexpandtab
