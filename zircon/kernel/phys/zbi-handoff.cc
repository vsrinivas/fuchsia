// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "zbi-handoff.h"

#include <ktl/byte.h>
#include <ktl/span.h>

#include "phys/handoff.h"

void SummarizeMiscZbiItems(PhysHandoff& handoff, ktl::span<ktl::byte> zbi) {
  handoff.zbi = reinterpret_cast<uintptr_t>(zbi.data());
}
