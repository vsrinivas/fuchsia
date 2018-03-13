// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_KERNEL_H_
#define GARNET_BIN_GUEST_KERNEL_H_

#include "garnet/lib/machina/phys_mem.h"

static constexpr uintptr_t kRamdiskOffset = 0x4000000;

zx_status_t load_kernel(const std::string& kernel_path,
                        const machina::PhysMem& phys_mem,
                        const uintptr_t kernel_off);

#endif  // GARNET_BIN_GUEST_KERNEL_H_
