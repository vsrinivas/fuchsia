// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/pci/pciroot.h>
#include <lib/zx/bti.h>
#include <zircon/status.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/pciroot.h>

namespace board_qemu_arm64 {
class QemuArm64Pciroot : public PcirootBase {
 public:
  struct Context {
    pci_platform_info_t info;
  };
  static zx_status_t Create(PciRootHost* root_host, QemuArm64Pciroot::Context ctx,
                            zx_device_t* parent, const char* name);
  virtual zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) final;
  virtual zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* info) final;

 private:
  Context context_;
  QemuArm64Pciroot(PciRootHost* root_host, QemuArm64Pciroot::Context ctx, zx_device_t* parent,
                   const char* name)
      : PcirootBase(root_host, parent, name), context_(ctx) {}
};

}  // namespace board_qemu_arm64
