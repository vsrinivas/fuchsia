// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SYSMEM_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SYSMEM_H_

#include <fidl/fuchsia.hardware.platform.bus/cpp/wire.h>
#include <lib/ddk/device.h>
#include <zircon/compiler.h>

zx_status_t publish_sysmem(fidl::WireSyncClient<fuchsia_hardware_platform_bus::PlatformBus>& pbus);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SYSMEM_H_
