// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ZIRCON_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ZIRCON_H_

#include <fuchsia/virtualization/cpp/fidl.h>

#include "src/virtualization/bin/vmm/dev_mem.h"
#include "src/virtualization/bin/vmm/device/phys_mem.h"
#include "src/virtualization/bin/vmm/platform_device.h"

zx_status_t setup_zircon(const fuchsia::virtualization::GuestConfig& cfg, const PhysMem& phys_mem,
                         const DevMem& dev_mem, const std::vector<PlatformDevice*>& devices,
                         uintptr_t* guest_ip, uintptr_t* boot_ptr);

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ZIRCON_H_
