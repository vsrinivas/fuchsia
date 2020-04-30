// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_
#define SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/event.h>

#include <thread>

#include <ddktl/device.h>

namespace amlogic_ram {

// The AmlRam device provides FIDL services directly to applications
// to query performance counters. For example effective DDR bandwith.

class AmlRam;
using DeviceType = ddk::Device<AmlRam, ddk::Suspendable, ddk::Messageable>;

class AmlRam : public DeviceType {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlRam);

  static zx_status_t Create(void* context, zx_device_t* parent);

  explicit AmlRam(zx_device_t* parent, ddk::MmioBuffer mmio);
  void DdkRelease();
  void DdkSuspend(ddk::SuspendTxn txn);

  // Implements ddk::Messageable
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

 private:
  zx_status_t Bind();
  void ReadLoop();
  void Shutdown();

  ddk::MmioBuffer mmio_;
  std::thread thread_;
  zx::event shutdown_;
};

}  // namespace amlogic_ram

#endif  // SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_
