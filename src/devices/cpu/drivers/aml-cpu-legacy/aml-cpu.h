// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CPU_DRIVERS_AML_CPU_LEGACY_AML_CPU_H_
#define SRC_DEVICES_CPU_DRIVERS_AML_CPU_LEGACY_AML_CPU_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/cpu/ctrl/llcpp/fidl.h>
#include <fuchsia/hardware/thermal/llcpp/fidl.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace amlogic_cpu {

namespace fuchsia_cpuctrl = fuchsia_hardware_cpu_ctrl;
namespace fuchsia_thermal = fuchsia_hardware_thermal;

class AmlCpu;
using DeviceType = ddk::Device<AmlCpu, ddk::Messageable<fuchsia_cpuctrl::Device>::Mixin,
                               ddk::PerformanceTunable, ddk::AutoSuspendable>;

class AmlCpu : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_CPU_CTRL> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlCpu);
  AmlCpu(zx_device_t* device, fidl::WireSyncClient<fuchsia_thermal::Device> thermal_client,
         size_t power_domain_index, uint32_t cluster_core_count)
      : DeviceType(device),
        thermal_client_(std::move(thermal_client)),
        power_domain_index_(power_domain_index),
        cluster_core_count_(cluster_core_count) {}

  static zx_status_t Create(void* context, zx_device_t* device);

  // Implements DDK Device Ops
  void DdkRelease();

  // Implements ddk::PerformanceTunable.
  zx_status_t DdkSetPerformanceState(uint32_t requested_state, uint32_t* out_state);
  zx_status_t DdkConfigureAutoSuspend(bool enable, uint8_t requested_sleep_state);

  // Fidl server interface implementation.
  void GetPerformanceStateInfo(GetPerformanceStateInfoRequestView request,
                               GetPerformanceStateInfoCompleter::Sync& completer) override;
  void GetNumLogicalCores(GetNumLogicalCoresRequestView request,
                          GetNumLogicalCoresCompleter::Sync& completer) override;
  void GetLogicalCoreId(GetLogicalCoreIdRequestView request,
                        GetLogicalCoreIdCompleter::Sync& completer) override;

  // Set CpuInfo in inspect.
  void SetCpuInfo(uint32_t cpu_version_packed);

  // Accessor
  uint32_t ClusterCoreCount() const { return cluster_core_count_; }
  size_t PowerDomainIndex() const { return power_domain_index_; }

 private:
  zx_status_t GetThermalOperatingPoints(fuchsia_thermal::wire::OperatingPoint* out);
  fidl::WireSyncClient<fuchsia_thermal::Device> thermal_client_;
  size_t power_domain_index_;
  uint32_t cluster_core_count_;

 protected:
  inspect::Inspector inspector_;
  inspect::Node cpu_info_ = inspector_.GetRoot().CreateChild("cpu_info_service");
};

}  // namespace amlogic_cpu

#endif  // SRC_DEVICES_CPU_DRIVERS_AML_CPU_LEGACY_AML_CPU_H_
