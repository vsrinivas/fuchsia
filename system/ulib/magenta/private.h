// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls.h>

// This defines the struct shared with the kernel.
#include <lib/vdso-constants.h>

extern const struct vdso_constants DATA_CONSTANTS
    __attribute__((visibility("hidden")));

// This declares the VDSO_mx_* aliases for the vDSO entry points.
// Calls made from within the vDSO must use these names rather than
// the public names so as to avoid PLT entries.
#include <magenta/syscall-vdso-definitions.h>
