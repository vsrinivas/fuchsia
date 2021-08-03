// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/hypervisor/arch.h"

#include <fbl/span.h>

#include "src/virtualization/tests/hypervisor/hypervisor_tests.h"

void SetUpGuestPageTable(fbl::Span<uint8_t> guest_memory) {
  // ARM64 does not require a page table for basic execution.
}
