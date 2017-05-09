// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/syscalls.h>

// This defines the struct shared with the kernel.
#include <lib/vdso-constants.h>

extern __LOCAL const struct vdso_constants DATA_CONSTANTS;

extern "C" {

// This declares the VDSO_mx_* aliases for the vDSO entry points.
// Calls made from within the vDSO must use these names rather than
// the public names so as to avoid PLT entries.
#include <magenta/syscall-vdso-definitions.h>

__LOCAL decltype(mx_ticks_get) CODE_soft_ticks_get;

};

// Code should define '_mx_foo' and then do 'VDSO_PUBLIC_ALIAS(mx_foo);'.
#define VDSO_PUBLIC_ALIAS(name) decltype(name) name __WEAK_ALIAS("_" #name)

// This symbol is expected to appear in the build-time vDSO symbol table so
// kernel/lib/vdso/ code can use it.
#define VDSO_KERNEL_EXPORT __attribute__((used))
