# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/src/alias.c \
    $(LOCAL_DIR)/src/arith_yacc.c \
    $(LOCAL_DIR)/src/arith_yylex.c \
    $(LOCAL_DIR)/src/bltin/magenta.c \
    $(LOCAL_DIR)/src/bltin/printf.c \
    $(LOCAL_DIR)/src/bltin/test.c \
    $(LOCAL_DIR)/src/bltin/times.c \
    $(LOCAL_DIR)/src/builtins.c \
    $(LOCAL_DIR)/src/cd.c \
    $(LOCAL_DIR)/src/controller.c \
    $(LOCAL_DIR)/src/error.c \
    $(LOCAL_DIR)/src/eval.c \
    $(LOCAL_DIR)/src/exec.c \
    $(LOCAL_DIR)/src/expand.c \
    $(LOCAL_DIR)/src/init.c \
    $(LOCAL_DIR)/src/input.c \
    $(LOCAL_DIR)/src/jobs.c \
    $(LOCAL_DIR)/src/main.c \
    $(LOCAL_DIR)/src/memalloc.c \
    $(LOCAL_DIR)/src/miscbltin.c \
    $(LOCAL_DIR)/src/mystring.c \
    $(LOCAL_DIR)/src/nodes.c \
    $(LOCAL_DIR)/src/options.c \
    $(LOCAL_DIR)/src/output.c \
    $(LOCAL_DIR)/src/output.h \
    $(LOCAL_DIR)/src/parser.c \
    $(LOCAL_DIR)/src/process.c \
    $(LOCAL_DIR)/src/redir.c \
    $(LOCAL_DIR)/src/show.c \
    $(LOCAL_DIR)/src/signames.c \
    $(LOCAL_DIR)/src/syntax.c \
    $(LOCAL_DIR)/src/system.c \
    $(LOCAL_DIR)/src/tab.c \
    $(LOCAL_DIR)/src/trap.c \
    $(LOCAL_DIR)/src/var.c \

MODULE_NAME := sh

MODULE_STATIC_LIBS := system/ulib/pretty third_party/ulib/linenoise
MODULE_LIBS := system/ulib/magenta system/ulib/launchpad system/ulib/c
MODULE_LIBS += system/ulib/mxio  # Needed by ulib/linenoise

MODULE_CFLAGS := -D_GNU_SOURCE -DBSD -DIFS_BROKEN -DJOBS=0 -DSHELL \
                 -DUSE_LINENOISE
MODULE_CFLAGS += -include $(LOCAL_DIR)/config.h -I$(LOCAL_DIR)/src

# TODO: Fix Warnings
MODULE_CFLAGS += -Wno-error -Wno-strict-prototypes -Wno-sign-compare
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
MODULE_CFLAGS += -Wno-discarded-qualifiers
else
MODULE_CFLAGS += -Wno-incompatible-pointer-types-discards-qualifiers
MODULE_CFLAGS += -Wno-gnu-designator -Wno-format-security -Wno-string-plus-int
endif
MODULE_CFLAGS += -Wno-logical-not-parentheses

MODULE_DEFINES += DEBUG=1

include make/module.mk
