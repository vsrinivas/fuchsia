// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Architecture-specific implementations of common code.

#ifndef SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_H_
#define SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_H_

#include <lib/stdcompat/span.h>
#include <stdint.h>
#include <zircon/types.h>

#include "constants.h"

// Set up an identity page-table (mapping virt/phys 1:1) for the guest.
void SetUpGuestPageTable(cpp20::span<uint8_t> guest_memory);

// Entry point where the guest should start running.
constexpr zx_gpaddr_t kGuestEntryPoint = GUEST_ENTRY;

#endif  // SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_H_
