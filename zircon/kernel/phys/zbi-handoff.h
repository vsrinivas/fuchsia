// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_ZBI_HANDOFF_H_
#define ZIRCON_KERNEL_PHYS_ZBI_HANDOFF_H_

#include <ktl/byte.h>
#include <ktl/span.h>

#include "phys/handoff.h"

// Summarizes the provided data ZBI's various ZBI items for the kernel,
// encoding that into the hand-off.
void SummarizeMiscZbiItems(PhysHandoff& handoff, ktl::span<ktl::byte> zbi);

#endif  // ZIRCON_KERNEL_PHYS_ZBI_HANDOFF_H_
