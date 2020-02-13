// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CPU_DRIVERS_AML_CPU_AML_CPU_H_
#define SRC_DEVICES_CPU_DRIVERS_AML_CPU_AML_CPU_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/cpu/ctrl/llcpp/fidl.h>
#include <fuchsia/hardware/thermal/llcpp/fidl.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace amlogic_cpu {

namespace fuchsia_cpuctrl = ::llcpp::fuchsia::hardware::cpu::ctrl;
namespace fuchsia_thermal = ::llcpp::fuchsia::hardware::thermal;

class AmlCpu;
using DeviceType =
    ddk::Device<AmlCpu, ddk::Messageable, ddk::PerformanceTunable, ddk::AutoSuspendable>;

class AmlCpu : public DeviceType,
               public ddk::EmptyProtocol<ZX_PROTOCOL_CPU_CTRL>,
               fuchsia_cpuctrl::Device::Interface {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlCpu);
  explicit AmlCpu(zx_device_t* device, fuchsia_thermal::Device::SyncClient thermal_client)
      : DeviceType(device), thermal_client_(std::move(thermal_client)) {}

  static zx_status_t Create(void* context, zx_device_t* device);

  // Implements ddk::Messageable
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  // Implements DDK Device Ops
  void DdkRelease();

  // Implements ddk::PerformanceTunable.
  zx_status_t DdkSetPerformanceState(uint32_t requested_state, uint32_t* out_state);
  zx_status_t DdkConfigureAutoSuspend(bool enable, uint8_t requested_sleep_state);

  // Fidl server interface implementation.
  void GetPerformanceStateInfo(uint32_t state, GetPerformanceStateInfoCompleter::Sync completer);
  void GetNumLogicalCores(GetNumLogicalCoresCompleter::Sync completer);
  void GetLogicalCoreId(uint64_t index, GetLogicalCoreIdCompleter::Sync completer);

 private:
  zx_status_t GetThermalOperatingPoints(fuchsia_thermal::OperatingPoint* out);

  fuchsia_thermal::Device::SyncClient thermal_client_;
};

}  // namespace amlogic_cpu

#endif  // SRC_DEVICES_CPU_DRIVERS_AML_CPU_AML_CPU_H_
