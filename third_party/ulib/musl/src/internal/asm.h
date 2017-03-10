// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define ALIAS_ENTRY(name) \
    .globl name; \
    .type name, %function; \
name:

#define ALIAS_END(name) \
    .size name, . - name

#define ENTRY(name) \
    ALIAS_ENTRY(name) \
    .cfi_startproc

#define END(name) \
    .cfi_endproc; \
    ALIAS_END(name)
