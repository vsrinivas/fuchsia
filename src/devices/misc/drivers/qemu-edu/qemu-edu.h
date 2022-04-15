// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_MISC_DRIVERS_QEMU_EDU_QEMU_EDU_H_
#define SRC_DEVICES_MISC_DRIVERS_QEMU_EDU_QEMU_EDU_H_

#include <lib/inspect/cpp/inspector.h>

#include <ddktl/device.h>

namespace qemu_edu {

class QemuEduDevice;
using DeviceType =
    ddk::Device<QemuEduDevice>;

class QemuEduDevice : public DeviceType {
 public:
  QemuEduDevice(zx_device_t* device) : DeviceType(device) {}

  // Implement DDK Device Ops
  static zx_status_t Create(void* ctx, zx_device_t* parent);
  void DdkRelease() { delete this; }

 protected:
  inspect::Inspector inspector_;
  inspect::Node edu_info_ = inspector_.GetRoot().CreateChild("qemu_edu_device");

 private:
  std::mutex lock_;
};

}  // namespace qemu_edu

#endif  // SRC_DEVICES_MISC_DRIVERS_QEMU_EDU_QEMU_EDU_H_
