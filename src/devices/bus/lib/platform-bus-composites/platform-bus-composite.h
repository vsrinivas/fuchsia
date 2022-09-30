// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_LIB_PLATFORM_BUS_COMPOSITES_PLATFORM_BUS_COMPOSITE_H_
#define SRC_DEVICES_BUS_LIB_PLATFORM_BUS_COMPOSITES_PLATFORM_BUS_COMPOSITE_H_

#include <fidl/fuchsia.device.manager/cpp/wire_types.h>
#include <lib/ddk/driver.h>

namespace platform_bus_composite {

// Convert the given |fragments| into their FIDL equivalent.
fidl::VectorView<fuchsia_device_manager::wire::DeviceFragment> MakeFidlFragment(
    fidl::AnyArena& arena, const device_fragment_t* fragments, size_t fragment_count);

}  // namespace platform_bus_composite

#endif  // SRC_DEVICES_BUS_LIB_PLATFORM_BUS_COMPOSITES_PLATFORM_BUS_COMPOSITE_H_
