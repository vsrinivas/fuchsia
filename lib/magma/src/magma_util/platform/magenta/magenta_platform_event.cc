// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_event.h"
#include "magma_util/macros.h"

namespace magma {

std::unique_ptr<PlatformEvent> PlatformEvent::Create()
{
    mx::event event;
    mx_status_t status = mx::event::create(0, &event);
    if (status != MX_OK)
        return DRETP(nullptr, "event::create failed: %d", status);

    return std::make_unique<MagentaPlatformEvent>(std::move(event));
}

} // namespace magma
