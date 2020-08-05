// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-tsensor.h"

#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <cmath>

#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <hw/reg.h>

#include "aml-tsensor-regs.h"

namespace thermal {

namespace {

// MMIO indexes.
constexpr uint32_t kPllMmio = 0;
constexpr uint32_t kAoMmio = 1;
constexpr uint32_t kHiuMmio = 2;

// Thermal calibration magic numbers from uboot.
constexpr int32_t kCalA_ = 324;
constexpr int32_t kCalB_ = 424;
constexpr int32_t kCalC_ = 3159;
constexpr int32_t kCalD_ = 9411;
constexpr float kRebootTempCelsius = 130.0f;

}  // namespace

zx_status_t AmlTSensor::NotifyThermalDaemon() {
  zx_port_packet_t thermal_port_packet;
  thermal_port_packet.key = current_trip_idx_;
  thermal_port_packet.type = ZX_PKT_TYPE_USER;
  return zx_port_queue(port_, &thermal_port_packet);
}

void AmlTSensor::UpdateRiseThresholdIrq(uint32_t irq) {
  // Clear the IRQ.
  auto sensor_ctl = TsCfgReg1::Get().ReadFrom(&*pll_mmio_);
  auto reg_value = sensor_ctl.reg_value();

  // Disable the IRQ
  reg_value &= ~(1 << (IRQ_RISE_ENABLE_SHIFT + irq));
  // Enable corresponding Fall IRQ
  reg_value |= (1 << (IRQ_FALL_ENABLE_SHIFT + irq));
  // Clear Rise IRQ Stat.
  reg_value |= (1 << (IRQ_RISE_STAT_CLR_SHIFT + irq));
  sensor_ctl.set_reg_value(reg_value);
  sensor_ctl.WriteTo(&*pll_mmio_);

  // Write 0 to CLR_STAT bit.
  sensor_ctl = TsCfgReg1::Get().ReadFrom(&*pll_mmio_);
  reg_value = sensor_ctl.reg_value();
  reg_value &= ~(1 << (IRQ_RISE_STAT_CLR_SHIFT + irq));
  sensor_ctl.set_reg_value(reg_value);
  sensor_ctl.WriteTo(&*pll_mmio_);
}

void AmlTSensor::UpdateFallThresholdIrq(uint32_t irq) {
  // Clear the IRQ.
  auto sensor_ctl = TsCfgReg1::Get().ReadFrom(&*pll_mmio_);
  auto reg_value = sensor_ctl.reg_value();

  // Disable the IRQ
  reg_value &= ~(1 << (IRQ_FALL_ENABLE_SHIFT + irq));
  // Enable corresponding Rise IRQ
  reg_value |= (1 << (IRQ_RISE_ENABLE_SHIFT + irq));
  // Clear Fall IRQ Stat.
  reg_value |= (1 << (IRQ_FALL_STAT_CLR_SHIFT + irq));
  sensor_ctl.set_reg_value(reg_value);
  sensor_ctl.WriteTo(&*pll_mmio_);

  // Write 0 to CLR_STAT bit.
  sensor_ctl = TsCfgReg1::Get().ReadFrom(&*pll_mmio_);
  reg_value = sensor_ctl.reg_value();
  reg_value &= ~(1 << (IRQ_FALL_STAT_CLR_SHIFT + irq));
  sensor_ctl.set_reg_value(reg_value);
  sensor_ctl.WriteTo(&*pll_mmio_);
}

int AmlTSensor::TripPointIrqHandler() {
  zxlogf(INFO, "%s start", __func__);
  zx_status_t status = ZX_OK;

  // Notify thermal daemon about the default settings.
  status = NotifyThermalDaemon();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-tsensor: Failed to send packet via port");
    return status;
  }

  while (running_.load()) {
    status = tsensor_irq_.wait(NULL);
    if (status != ZX_OK) {
      return status;
    }

    auto irq_stat = TsStat1::Get().ReadFrom(&*pll_mmio_);

    if (irq_stat.reg_value() & AML_RISE_THRESHOLD_IRQ) {
      // Handle Rise threshold IRQs.
      if (irq_stat.rise_th3_irq()) {
        UpdateRiseThresholdIrq(3);
        current_trip_idx_ = 4;
      } else if (irq_stat.rise_th2_irq()) {
        UpdateRiseThresholdIrq(2);
        current_trip_idx_ = 3;
      } else if (irq_stat.rise_th1_irq()) {
        UpdateRiseThresholdIrq(1);
        current_trip_idx_ = 2;
      } else if (irq_stat.rise_th0_irq()) {
        UpdateRiseThresholdIrq(0);
        current_trip_idx_ = 1;
      }
    } else if (irq_stat.reg_value() & AML_FALL_THRESHOLD_IRQ) {
      // Handle Fall threshold IRQs.
      if (irq_stat.fall_th3_irq()) {
        UpdateFallThresholdIrq(3);
        current_trip_idx_ = 3;
      } else if (irq_stat.fall_th2_irq()) {
        UpdateFallThresholdIrq(2);
        current_trip_idx_ = 2;
      } else if (irq_stat.fall_th1_irq()) {
        UpdateFallThresholdIrq(1);
        current_trip_idx_ = 1;
      } else if (irq_stat.fall_th0_irq()) {
        UpdateFallThresholdIrq(0);
        current_trip_idx_ = 0;
      }
    } else {
      // Spurious interrupt
      continue;
    }

    // Notify thermal daemon about new trip point.
    status = NotifyThermalDaemon();
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-tsensor: Failed to send packet via port");
      return status;
    }
  }
  return status;
}

zx_status_t AmlTSensor::InitTripPoints() {
  if (thermal_config_.trip_point_info[thermal_config_.num_trip_points].up_temp_celsius !=
      (-273.15f + 2.0f)) {
    return ZX_ERR_INTERNAL;
  }

  auto set_thresholds = [this](auto&& rise_threshold, auto&& fall_threshold, uint32_t i) {
    auto rise_temperature =
        TempCelsiusToCode(thermal_config_.trip_point_info[i].up_temp_celsius, true);
    auto fall_temperature =
        TempCelsiusToCode(thermal_config_.trip_point_info[i].down_temp_celsius, false);

    // Program the 2 rise temperature thresholds.
    if (i % 2) {
      rise_threshold.ReadFrom(&*pll_mmio_).set_rise_th0(rise_temperature).WriteTo(&*pll_mmio_);
      fall_threshold.ReadFrom(&*pll_mmio_).set_fall_th0(fall_temperature).WriteTo(&*pll_mmio_);
    } else {
      rise_threshold.ReadFrom(&*pll_mmio_).set_rise_th1(rise_temperature).WriteTo(&*pll_mmio_);
      fall_threshold.ReadFrom(&*pll_mmio_).set_fall_th1(fall_temperature).WriteTo(&*pll_mmio_);
    }
  };

  // Set rise and fall trip points for the first 4 trip points, since the HW supports only 4.
  // We skip the 1st entry since it's the default setting for boot up.
  switch (thermal_config_.num_trip_points) {
    default:
    case 5:
      set_thresholds(TsCfgReg5::Get(), TsCfgReg7::Get(), 4);
      [[fallthrough]];
    case 4:
      set_thresholds(TsCfgReg5::Get(), TsCfgReg7::Get(), 3);
      [[fallthrough]];
    case 3:
      set_thresholds(TsCfgReg4::Get(), TsCfgReg6::Get(), 2);
      [[fallthrough]];
    case 2:
      set_thresholds(TsCfgReg4::Get(), TsCfgReg6::Get(), 1);
      [[fallthrough]];
    case 1:
      [[fallthrough]];
    case 0:
      break;
  }

  // Clear all IRQ's status.
  TsCfgReg1::Get()
      .ReadFrom(&*pll_mmio_)
      .set_fall_th3_irq_stat_clr(1)
      .set_fall_th2_irq_stat_clr(1)
      .set_fall_th1_irq_stat_clr(1)
      .set_fall_th0_irq_stat_clr(1)
      .set_rise_th3_irq_stat_clr(1)
      .set_rise_th2_irq_stat_clr(1)
      .set_rise_th1_irq_stat_clr(1)
      .set_rise_th0_irq_stat_clr(1)
      .WriteTo(&*pll_mmio_);

  TsCfgReg1::Get()
      .ReadFrom(&*pll_mmio_)
      .set_fall_th3_irq_stat_clr(0)
      .set_fall_th2_irq_stat_clr(0)
      .set_fall_th1_irq_stat_clr(0)
      .set_fall_th0_irq_stat_clr(0)
      .set_rise_th3_irq_stat_clr(0)
      .set_rise_th2_irq_stat_clr(0)
      .set_rise_th1_irq_stat_clr(0)
      .set_rise_th0_irq_stat_clr(0)
      .WriteTo(&*pll_mmio_);

  // Enable all IRQs.
  auto ts_cfg_reg1 = TsCfgReg1::Get().ReadFrom(&*pll_mmio_);
  switch (thermal_config_.num_trip_points) {
    default:
    case 5:
      ts_cfg_reg1.set_rise_th3_irq_en(1);
      [[fallthrough]];
    case 4:
      ts_cfg_reg1.set_rise_th2_irq_en(1);
      [[fallthrough]];
    case 3:
      ts_cfg_reg1.set_rise_th1_irq_en(1);
      [[fallthrough]];
    case 2:
      ts_cfg_reg1.set_rise_th0_irq_en(1);
      [[fallthrough]];
    case 1:
    case 0:
      break;
  }
  ts_cfg_reg1.set_enable_irq(1).WriteTo(&*pll_mmio_);

  // Start thermal notification thread.
  auto start_thread = [](void* arg) -> int {
    return static_cast<AmlTSensor*>(arg)->TripPointIrqHandler();
  };

  running_.store(true);
  int rc = thrd_create_with_name(&irq_thread_, start_thread, this, "aml_tsendor_irq_thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

// Tsensor treats temperature as a mapped temperature code.
// The temperature is converted differently depending on the calibration type.
uint32_t AmlTSensor::TempCelsiusToCode(float temp_c, bool trend) {
  int32_t temp_decicelsius = static_cast<int32_t>(std::round(temp_c * 10.0f));
  int64_t sensor_code;
  uint32_t reg_code;
  uint32_t uefuse = trim_info_ & 0xffff;

  // Referred u-boot code for below magic calculations.
  // T = 727.8*(u_real+u_efuse/(1<<16)) - 274.7
  // u_readl = (5.05*YOUT)/((1<<16)+ 4.05*YOUT)
  // u_readl = (T + 274.7) / 727.8 - u_efuse / (1 << 16)
  // Yout =  (u_readl / (5.05 - 4.05u_readl)) *(1 << 16)
  if (uefuse & 0x8000) {
    sensor_code = ((1 << 16) * (temp_decicelsius + kCalC_) / kCalD_ +
                   (1 << 16) * (uefuse & 0x7fff) / (1 << 16));
  } else {
    sensor_code = ((1 << 16) * (temp_decicelsius + kCalC_) / kCalD_ -
                   (1 << 16) * (uefuse & 0x7fff) / (1 << 16));
  }

  sensor_code = (sensor_code * 100 / (kCalB_ - kCalA_ * sensor_code / (1 << 16)));
  if (trend) {
    reg_code = static_cast<uint32_t>((sensor_code >> 0x4) & AML_TS_TEMP_MASK) + AML_TEMP_CAL;
  } else {
    reg_code = ((sensor_code >> 0x4) & AML_TS_TEMP_MASK);
  }
  return reg_code;
}

// Calculate a temperature value from a temperature code.
// The unit of the temperature is degree Celsius.
float AmlTSensor::CodeToTempCelsius(uint32_t temp_code) {
  uint32_t sensor_temp = temp_code;
  uint32_t uefuse = trim_info_ & 0xffff;

  // Referred u-boot code for below magic calculations.
  // T = 727.8*(u_real+u_efuse/(1<<16)) - 274.7
  // u_readl = (5.05*YOUT)/((1<<16)+ 4.05*YOUT)
  sensor_temp =
      ((sensor_temp * kCalB_) / 100 * (1 << 16) / (1 * (1 << 16) + kCalA_ * sensor_temp / 100));
  if (uefuse & 0x8000) {
    sensor_temp = ((sensor_temp - (uefuse & (0x7fff))) * kCalD_ / (1 << 16) - kCalC_);
  } else {
    sensor_temp = ((sensor_temp + uefuse) * kCalD_ / (1 << 16) - kCalC_);
  }
  return static_cast<float>(sensor_temp) / 10.0f;
}

float AmlTSensor::ReadTemperatureCelsius() {
  int count = 0;
  unsigned int value_all = 0;

  // Datasheet is incorrect.
  // Referred to u-boot code.
  // Yay magic numbers.
  for (int j = 0; j < AML_TS_VALUE_CONT; j++) {
    auto ts_stat0 = TsStat0::Get().ReadFrom(&*pll_mmio_);
    auto tvalue = ts_stat0.temperature();

    if ((tvalue >= 0x18a9) && (tvalue <= 0x32a6)) {
      count++;
      value_all += tvalue;
    }
  }
  if (count == 0) {
    return 0;
  } else {
    return CodeToTempCelsius(value_all / count);
  }
}

void AmlTSensor::SetRebootTemperatureCelsius(uint32_t temp_c) {
  uint32_t reboot_val = TempCelsiusToCode(kRebootTempCelsius, true);
  auto reboot_config = TsCfgReg2::Get().ReadFrom(&*pll_mmio_);

  reboot_config.set_hi_temp_enable(1)
      .set_reset_en(1)
      .set_high_temp_times(AML_TS_REBOOT_TIME)
      .set_high_temp_threshold(reboot_val << 4)
      .WriteTo(&*pll_mmio_);
}

zx_status_t AmlTSensor::GetStateChangePort(zx_handle_t* port) {
  return zx_handle_duplicate(port_, ZX_RIGHT_SAME_RIGHTS, port);
}

zx_status_t AmlTSensor::Create(zx_device_t* parent,
                               fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config) {
  // InitPdev
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map amlogic temperature sensor peripheral control registers.
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, kPllMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-tsensor: could not map periph mmio: %d", status);
    return status;
  }
  pll_mmio_ = ddk::MmioBuffer(mmio);

  status = pdev_map_mmio_buffer(&pdev_, kAoMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-tsensor: could not map periph mmio: %d", status);
    return status;
  }
  ao_mmio_ = ddk::MmioBuffer(mmio);

  status = pdev_map_mmio_buffer(&pdev_, kHiuMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-tsensor: could not map periph mmio: %d", status);
    return status;
  }
  hiu_mmio_ = ddk::MmioBuffer(mmio);

  // Map tsensor interrupt.
  status = pdev_get_interrupt(&pdev_, 0, 0, tsensor_irq_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-tsensor: could not map tsensor interrupt");
    return status;
  }

  return InitSensor(thermal_config);
}

zx_status_t AmlTSensor::InitSensor(fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config) {
  // Copy the thermal_config
  memcpy(&thermal_config_, &thermal_config, sizeof(fuchsia_hardware_thermal_ThermalDeviceInfo));

  // Get the trim info.
  trim_info_ = ao_mmio_->Read32(AML_TRIM_INFO);

  // Set the clk.
  hiu_mmio_->Write32(AML_HHI_TS_CLK_ENABLE, AML_HHI_TS_CLK_CNTL);

  // Not setting IRQ's here.
  auto sensor_ctl = TsCfgReg1::Get().ReadFrom(&*pll_mmio_);
  sensor_ctl.set_filter_en(1)
      .set_ts_ana_en_vcm(1)
      .set_ts_ana_en_vbg(1)
      .set_bipolar_bias_current_input(AML_TS_CH_SEL)
      .set_ts_ena_en_iptat(1)
      .set_ts_dem_en(1)
      .WriteTo(&*pll_mmio_);

  // Create a port to send messages to thermal daemon.
  zx_status_t status = zx_port_create(0, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-tsensor: Unable to create port");
    return status;
  }

  // Setup IRQ's and rise/fall thresholds.
  return InitTripPoints();
}

AmlTSensor::~AmlTSensor() {
  running_.store(false);
  thrd_join(irq_thread_, NULL);
  tsensor_irq_.destroy();
}

}  // namespace thermal
