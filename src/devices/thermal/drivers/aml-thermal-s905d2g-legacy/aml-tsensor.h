// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_LEGACY_AML_TSENSOR_H_
#define SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_LEGACY_AML_TSENSOR_H_

#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <threads.h>

#include <atomic>
#include <optional>

#include <ddktl/device.h>
#include <fbl/macros.h>

namespace thermal {

// This class represents a temperature sensor
// which is on the S905D2 core.
class AmlTSensor {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlTSensor);
  AmlTSensor() {}
  // For testing
  AmlTSensor(fdf::MmioBuffer pll_mmio, fdf::MmioBuffer trim_mmio, fdf::MmioBuffer hiu_mmio)
      : pll_mmio_(std::move(pll_mmio)),
        trim_mmio_(std::move(trim_mmio)),
        hiu_mmio_(std::move(hiu_mmio)) {}
  float ReadTemperatureCelsius();
  zx_status_t Create(zx_device_t* parent,
                     fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config);
  zx_status_t InitSensor(fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config);
  zx_status_t GetStateChangePort(zx_handle_t* port);
  ~AmlTSensor();

 private:
  int TripPointIrqHandler();
  uint32_t TempCelsiusToCode(float temp_c, bool trend);
  float CodeToTempCelsius(uint32_t temp_code);
  void SetRebootTemperatureCelsius(float temp_c);
  zx_status_t InitTripPoints();
  zx_status_t NotifyThermalDaemon();
  void UpdateFallThresholdIrq(uint32_t irq);
  void UpdateRiseThresholdIrq(uint32_t irq);

  uint32_t trim_info_;
  std::optional<fdf::MmioBuffer> pll_mmio_;
  std::optional<fdf::MmioBuffer> trim_mmio_;
  std::optional<fdf::MmioBuffer> hiu_mmio_;
  zx::interrupt tsensor_irq_;
  std::optional<thrd_t> irq_thread_;
  std::atomic<bool> running_;
  zx_handle_t port_;
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config_;
  uint32_t current_trip_idx_ = 0;
};
}  // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_LEGACY_AML_TSENSOR_H_
