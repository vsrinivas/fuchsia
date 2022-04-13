// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ACPI_DRIVERS_INTEL_THERMAL_INTEL_THERMAL_H_
#define SRC_DEVICES_ACPI_DRIVERS_INTEL_THERMAL_INTEL_THERMAL_H_

#include <fidl/fuchsia.hardware.acpi/cpp/markers.h>
#include <fidl/fuchsia.hardware.thermal/cpp/wire.h>
#include <fidl/fuchsia.hardware.thermal/cpp/wire_types.h>
#include <lib/async/dispatcher.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"

namespace intel_thermal {

inline constexpr uint32_t kTypeThermalSensor = 0x03;
inline constexpr uint32_t kThermalEvent = 0x90;

class IntelThermal;
using DeviceType = ddk::Device<IntelThermal, ddk::Initializable,
                               ddk::Messageable<fuchsia_hardware_thermal::Device>::Mixin>;
class IntelThermal : public DeviceType, fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler> {
 public:
  explicit IntelThermal(zx_device_t* parent, acpi::Client acpi, async_dispatcher_t* dispatcher)
      : DeviceType(parent), acpi_(std::move(acpi)), dispatcher_(dispatcher) {}
  virtual ~IntelThermal() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // Thermal FIDL methods.
  void GetDeviceInfo(GetDeviceInfoRequestView request,
                     GetDeviceInfoCompleter::Sync& completer) override;
  void GetDvfsInfo(GetDvfsInfoRequestView request, GetDvfsInfoCompleter::Sync& completer) override;
  void GetDvfsOperatingPoint(GetDvfsOperatingPointRequestView request,
                             GetDvfsOperatingPointCompleter::Sync& completer) override;
  void GetFanLevel(GetFanLevelRequestView request, GetFanLevelCompleter::Sync& completer) override;
  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) override;
  void GetStateChangeEvent(GetStateChangeEventRequestView request,
                           GetStateChangeEventCompleter::Sync& completer) override;
  void GetStateChangePort(GetStateChangePortRequestView request,
                          GetStateChangePortCompleter::Sync& completer) override;
  void GetTemperatureCelsius(GetTemperatureCelsiusRequestView request,
                             GetTemperatureCelsiusCompleter::Sync& completer) override;
  void SetDvfsOperatingPoint(SetDvfsOperatingPointRequestView request,
                             SetDvfsOperatingPointCompleter::Sync& completer) override;
  void SetFanLevel(SetFanLevelRequestView request, SetFanLevelCompleter::Sync& completer) override;
  void SetTripCelsius(SetTripCelsiusRequestView request,
                      SetTripCelsiusCompleter::Sync& completer) override;

  // ACPI NotifyHandler FIDL methods.
  void Handle(HandleRequestView request, HandleCompleter::Sync& completer) override;

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
  acpi::Client acpi_;
  async_dispatcher_t* dispatcher_ = nullptr;
  zx::event event_;
  uint32_t trip_point_count_ __TA_GUARDED(lock_);
  bool have_trip_[fuchsia_hardware_thermal::wire::kMaxTripPoints] __TA_GUARDED(lock_) = {false};
  float trip_points_[fuchsia_hardware_thermal::wire::kMaxTripPoints] __TA_GUARDED(lock_) = {0};
  std::mutex lock_;

  zx::status<uint64_t> EvaluateInteger(const char* name);
};

}  // namespace intel_thermal

#endif  // SRC_DEVICES_ACPI_DRIVERS_INTEL_THERMAL_INTEL_THERMAL_H_
