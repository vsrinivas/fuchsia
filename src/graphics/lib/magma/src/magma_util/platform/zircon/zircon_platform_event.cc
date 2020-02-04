// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_event.h"

#include "magma_util/macros.h"

namespace magma {

std::unique_ptr<PlatformEvent> PlatformEvent::Create() {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK)
    return DRETP(nullptr, "event::create failed: %d", status);

  return std::make_unique<ZirconPlatformEvent>(std::move(event));
}

}  // namespace magma
