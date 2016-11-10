// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This defines the struct shared with the kernel.
#include <lib/vdso-constants.h>

extern const struct vdso_constants DATA_CONSTANTS
    __attribute__((visibility("hidden")));
