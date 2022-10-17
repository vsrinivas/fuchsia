// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_COMPOSITE_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_COMPOSITE_H_

#include <fidl/fuchsia.device.composite/cpp/wire.h>
#include <lib/ddk/driver.h>

namespace compat {

zx::result<fuchsia_device_manager::wire::CompositeDeviceDescriptor> CreateComposite(
    fidl::AnyArena& arena, const composite_device_desc_t* comp_desc);
}

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_COMPOSITE_H_
