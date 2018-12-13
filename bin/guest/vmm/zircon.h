// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_ZIRCON_H_
#define GARNET_BIN_GUEST_VMM_ZIRCON_H_

#include "garnet/bin/guest/vmm/dev_mem.h"
#include "garnet/bin/guest/vmm/device/phys_mem.h"
#include "garnet/bin/guest/vmm/platform_device.h"

class GuestConfig;

zx_status_t setup_zircon(const GuestConfig& cfg, const PhysMem& phys_mem,
                         const DevMem& dev_mem,
                         const std::vector<PlatformDevice*>& devices,
                         uintptr_t* guest_ip, uintptr_t* boot_ptr);

#endif  // GARNET_BIN_GUEST_VMM_ZIRCON_H_
