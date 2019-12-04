// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_CPU_AML_CPU_AML_CPU_H_
#define ZIRCON_SYSTEM_DEV_CPU_AML_CPU_AML_CPU_H_

#include <fuchsia/hardware/cpu/ctrl/llcpp/fidl.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace amlogic_cpu {

namespace fuchsia_cpuctrl = ::llcpp::fuchsia::hardware::cpu::ctrl;

class AmlCpu;
using DeviceType = ddk::Device<AmlCpu, ddk::UnbindableNew, ddk::Messageable>;

class AmlCpu : public DeviceType,
               public ddk::EmptyProtocol<ZX_PROTOCOL_CPU_CTRL>,
               fuchsia_cpuctrl::Device::Interface {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlCpu);
  explicit AmlCpu(zx_device_t* device) : DeviceType(device) {}

  static zx_status_t Create(void* context, zx_device_t* device);

  // Implements Messageable
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn) {}

  // Fidl server interface implementation.
  void GetPerformanceStateInfo(uint32_t state, GetPerformanceStateInfoCompleter::Sync completer);
  void GetNumLogicalCores(GetNumLogicalCoresCompleter::Sync completer);
  void GetLogicalCoreId(uint64_t index, GetLogicalCoreIdCompleter::Sync completer);
};

}  // namespace amlogic_cpu

#endif  // ZIRCON_SYSTEM_DEV_CPU_AML_CPU_AML_CPU_H_
