# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# copy this and makefile to your external root directory and customize
# according to how you want to use a split repository

# the top level directory that all paths are relative to
#LKMAKEROOT := .

# paths relative to LKMAKEROOT where additional modules should be searched
#LKINC :=

# the path relative to LKMAKEROOT where the main lk repository lives
#LKROOT := .

# set the directory relative to LKMAKEROOT where output will go
#BUILDROOT ?= .

# set the default project if no args are passed
DEFAULT_PROJECT ?= magenta-qemu-arm64

#TOOLCHAIN_PREFIX := <relative path to toolchain>
