// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <fbl/array.h>
#include <fbl/ref_ptr.h>
#include "device-internal.h"

namespace devmgr {

typedef fbl::Array<fbl::RefPtr<zx_device>> CompositeComponents;

// Modifies |device| to have the appropriate protocol_id, ctx, and ops tables
// for a composite device
zx_status_t InitializeCompositeDevice(const fbl::RefPtr<zx_device>& device,
                                      CompositeComponents&& components);

} // namespace devmgr
