// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ACPI_DRIVERS_ACPI_BATTERY_ACPI_BATTERY_H_
#define SRC_DEVICES_ACPI_DRIVERS_ACPI_BATTERY_ACPI_BATTERY_H_

#include <fidl/fuchsia.hardware.power/cpp/wire.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"

namespace acpi_battery {

namespace fpower = fuchsia_hardware_power::wire;

// Fields in _BIF, per ACPI Spec 6.4 section 10.2.2.2, "_BIF (Battery Information)".
enum BifFields {
  kPowerUnit = 0,
  kDesignCapacity = 1,
  kLastFullChargeCapacity = 2,
  kBatteryTechnology = 3,
  kDesignVoltage = 4,
  kDesignCapacityWarning = 5,
  kDesignCapacityLow = 6,
  kCapacityGranularity1 = 7,
  kCapacityGranularity2 = 8,
  kModelNumber = 9,
  kSerialNumber = 10,
  kBatteryType = 11,
  kOemInformation = 12,
  kBifMax = 13,
};

// Fields in _BST, per ACPI Spec 6.4 section 10.2.2.11, "_BST (Battery Status)".
enum BstFields {
  kBatteryState = 0,
  kBatteryCurrentRate = 1,
  kBatteryRemainingCapacity = 2,
  kBatteryCurrentVoltage = 3,
  kBstMax = 4,
};

// Bits in the kBatteryState field of _BST.
enum AcpiBatteryState {
  kDischarging = (1 << 0),
  kCharging = (1 << 1),
  kCritical = (1 << 2),
  kChargeLimiting = (1 << 3),
};

// Battery statuses, per ACPI Spec 6.4 Table 5.156.
enum BatteryStatusNotification {
  kBatteryStatusChanged = 0x80,
  kBatteryInformationChanged = 0x81,
};

constexpr zx::duration kAcpiEventNotifyLimit = zx::msec(10);

class AcpiBattery;
using DeviceType = ddk::Device<AcpiBattery, ddk::Initializable,
                               ddk::Messageable<fuchsia_hardware_power::Source>::Mixin>;
// Note that we don't use ddk::Messageable for NotifyHandler because we only use it directly with
// ACPI.
class AcpiBattery : public DeviceType, fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler> {
 public:
  explicit AcpiBattery(zx_device_t* parent, acpi::Client acpi)
      : DeviceType(parent), acpi_(std::move(acpi)) {}

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // FIDL methods.
  void GetPowerInfo(GetPowerInfoRequestView request,
                    GetPowerInfoCompleter::Sync& completer) override;
  void GetStateChangeEvent(GetStateChangeEventRequestView request,
                           GetStateChangeEventCompleter::Sync& completer) override;
  void GetBatteryInfo(GetBatteryInfoRequestView request,
                      GetBatteryInfoCompleter::Sync& completer) override;

  void Handle(HandleRequestView request, HandleCompleter::Sync& completer) override;

  // Sets a signal on state_event_, notifying clients that power source state has changed.
  zx_status_t SignalClient() __TA_REQUIRES(lock_);
  // Clears the above state.
  zx_status_t ClearSignal() __TA_REQUIRES(lock_);

  // Calls _STA.
  zx_status_t CheckAcpiState();
  // Calls _BIF.
  zx_status_t CheckAcpiBatteryInformation();
  // Calls _BST.
  zx_status_t CheckAcpiBatteryState();

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
  acpi::Client acpi_;

  std::mutex lock_;
  zx::event state_event_ __TA_GUARDED(lock_);
  fpower::BatteryInfo battery_info_ __TA_GUARDED(lock_);
  fpower::SourceInfo source_info_ __TA_GUARDED(lock_) = {
      .type = fpower::PowerType::kBattery,
  };
  zx::time last_notify_timestamp_ = zx::time::infinite_past();

  inspect::StringProperty model_number_ =
      inspect_.GetRoot().CreateString("model-number", "UNKNOWN");
  inspect::StringProperty serial_number_ =
      inspect_.GetRoot().CreateString("serial-number", "UNKNOWN");
  inspect::StringProperty battery_type_ =
      inspect_.GetRoot().CreateString("battery-type", "UNKNOWN");
};

}  // namespace acpi_battery

#endif  // SRC_DEVICES_ACPI_DRIVERS_ACPI_BATTERY_ACPI_BATTERY_H_
