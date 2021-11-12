// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "zbi-handoff.h"

#include <lib/zbitl/view.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/span.h>

#include "phys/handoff.h"

void SummarizeMiscZbiItems(PhysHandoff& handoff, ktl::span<ktl::byte> zbi) {
  handoff.zbi = reinterpret_cast<uintptr_t>(zbi.data());

  zbitl::View view(zbi);
  for (auto it = view.begin(); it != view.end(); ++it) {
    auto [header, payload] = *it;
    switch (header->type) {
      case ZBI_TYPE_PLATFORM_ID:
        ZX_ASSERT(payload.size() >= sizeof(zbi_platform_id_t));
        handoff.platform_id = *reinterpret_cast<zbi_platform_id_t*>(payload.data());
        break;
    }
  }

  // At this point we should have full confidence that the ZBI is properly
  // formatted.
  ZX_ASSERT(view.take_error().is_ok());
}
