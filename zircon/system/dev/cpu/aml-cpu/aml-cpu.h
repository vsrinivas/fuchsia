// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>

namespace amlogic_cpu {

class AmlCpu;
using DeviceType = ddk::Device<AmlCpu, ddk::UnbindableNew, ddk::Messageable>;


class AmlCpu : public DeviceType {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlCpu);
  explicit AmlCpu(zx_device_t* device) : DeviceType(device) {}

  static zx_status_t Create(void* context, zx_device_t* device);

  // Implements Messageable
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn) {}

};

}  // namespace amlogic_cpu
