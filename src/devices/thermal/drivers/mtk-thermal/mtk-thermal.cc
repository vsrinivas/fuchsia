// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-thermal.h"

#include <fuchsia/hardware/clock/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <zircon/rights.h>
#include <zircon/threads.h>

#include <cmath>
#include <memory>

#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <soc/mt8167/mt8167-hw.h>

#include "src/devices/thermal/drivers/mtk-thermal/mtk-thermal-bind.h"

namespace {

constexpr uint32_t kTsCon1Addr = 0x10018604;
constexpr uint32_t kAuxAdcCon1SetAddr = 0x11003008;
constexpr uint32_t kAuxAdcCon1ClrAddr = 0x1100300c;
constexpr uint32_t kAuxAdcDat11Addr = 0x11003040;
constexpr uint32_t kAuxAdcChannel = 11;
constexpr uint32_t kAuxAdcBits = 12;

constexpr uint32_t kSensorCount = 3;

constexpr uint32_t kSrcClkFreq = 66'000'000;
constexpr uint32_t kSrcClkDivider = 256;

constexpr uint32_t FreqToPeriodUnits(uint32_t freq_hz, uint32_t period) {
  return (kSrcClkFreq / (kSrcClkDivider * (period + 1) * freq_hz)) - 1;
}

constexpr uint32_t kThermalPeriod = 1023;
constexpr uint32_t kFilterInterval = 0;
constexpr uint32_t kSenseInterval = FreqToPeriodUnits(10, kThermalPeriod);
constexpr uint32_t kAhbPollPeriod = FreqToPeriodUnits(10, kThermalPeriod);

constexpr int32_t FixedPoint(int32_t value) { return (value * 10000) >> 12; }

constexpr int32_t RawWithGain(int32_t raw, int32_t gain) {
  return (FixedPoint(raw) * 10000) / gain;
}

constexpr int32_t TempWithoutGain(int32_t temp, int32_t gain) {
  return (((temp * gain) / 10000) << 12) / 10000;
}

}  // namespace

namespace thermal {

zx_status_t MtkThermal::Create(void* context, zx_device_t* parent) {
  zx_status_t status;

  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_COMPOSITE not available", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(composite);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_device_info_t info;
  if ((status = pdev.GetDeviceInfo(&info)) != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_get_device_info failed", __FILE__);
    return status;
  }

  std::optional<ddk::MmioBuffer> mmio;
  if ((status = pdev.MapMmio(0, &mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed", __FILE__);
    return status;
  }

  std::optional<ddk::MmioBuffer> fuse_mmio;
  if ((status = pdev.MapMmio(1, &fuse_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed", __FILE__);
    return status;
  }

  std::optional<ddk::MmioBuffer> pll_mmio;
  if ((status = pdev.MapMmio(2, &pll_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed", __FILE__);
    return status;
  }

  std::optional<ddk::MmioBuffer> pmic_mmio;
  if ((status = pdev.MapMmio(3, &pmic_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed", __FILE__);
    return status;
  }

  std::optional<ddk::MmioBuffer> infracfg_mmio;
  if ((status = pdev.MapMmio(4, &infracfg_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed", __FILE__);
    return status;
  }

  size_t actual;
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info;
  status = device_get_metadata(parent, DEVICE_METADATA_THERMAL_CONFIG, &thermal_info,
                               sizeof(thermal_info), &actual);
  if (status != ZX_OK || actual != sizeof(thermal_info)) {
    zxlogf(ERROR, "%s: device_get_metadata failed", __FILE__);
    return status == ZX_OK ? ZX_ERR_INTERNAL : status;
  }

  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get interrupt", __FILE__);
    return status;
  }

  zx::port port;
  if ((status = zx::port::create(0, &port)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create port", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<MtkThermal> device(new (&ac) MtkThermal(
      parent, std::move(*mmio), std::move(*pll_mmio), std::move(*pmic_mmio),
      std::move(*infracfg_mmio), composite, pdev, thermal_info, std::move(port), std::move(irq),
      TempCalibration0::Get().ReadFrom(&(*fuse_mmio)),
      TempCalibration1::Get().ReadFrom(&(*fuse_mmio)),
      TempCalibration2::Get().ReadFrom(&(*fuse_mmio))));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: MtkThermal alloc failed", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("mtk-thermal")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed", __FILE__);
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t MtkThermal::Init() {
  auto fragment_count = composite_.GetFragmentCount();

  composite_device_fragment_t fragments[fragment_count];
  size_t actual;
  composite_.GetFragments(fragments, fragment_count, &actual);
  if (fragment_count != actual) {
    return ZX_ERR_INTERNAL;
  }

  // zeroth fragment is pdev
  for (uint32_t i = 1; i < fragment_count; i++) {
    clock_protocol_t clock;
    auto status = device_get_protocol(fragments[i].device, ZX_PROTOCOL_CLOCK, &clock);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to get clock %u", __FILE__, i);
      return status;
    }

    status = clock_enable(&clock);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to enable clock %u", __FILE__, i);
      return status;
    }
  }

  // Set the initial DVFS operating point. The bootloader sets it to 1.001 GHz @ 1.2 V.
  uint32_t op_idx =
      thermal_info_.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].count - 1;
  auto status = SetDvfsOpp(static_cast<uint16_t>(op_idx));
  if (status != ZX_OK) {
    return status;
  }

  TempMonCtl0::Get().ReadFrom(&mmio_).disable_all().WriteTo(&mmio_);

  TempMsrCtl0::Get()
      .ReadFrom(&mmio_)
      .set_msrctl0(TempMsrCtl0::kSample1)
      .set_msrctl1(TempMsrCtl0::kSample1)
      .set_msrctl2(TempMsrCtl0::kSample1)
      .set_msrctl3(TempMsrCtl0::kSample1)
      .WriteTo(&mmio_);

  TempAhbTimeout::Get().FromValue(0xffffffff).WriteTo(&mmio_);
  TempAdcPnp::Get(0).FromValue(0).WriteTo(&mmio_);
  TempAdcPnp::Get(1).FromValue(1).WriteTo(&mmio_);
  TempAdcPnp::Get(2).FromValue(2).WriteTo(&mmio_);

  // Set the thermal controller to read from the spare registers, then wait for the dummy sensor
  // reading to end up in TempMsr0-2.
  TempMonCtl1::Get().ReadFrom(&mmio_).set_period(1).WriteTo(&mmio_);
  TempMonCtl2::Get().ReadFrom(&mmio_).set_sen_interval(1).WriteTo(&mmio_);
  TempAhbPoll::Get().FromValue(1).WriteTo(&mmio_);

  constexpr uint32_t dummy_temp = (1 << kAuxAdcBits) - 1;
  TempSpare::Get(0).FromValue(dummy_temp | (1 << kAuxAdcBits)).WriteTo(&mmio_);

  TempPnpMuxAddr::Get().FromValue(TempSpare::Get(2).addr() + MT8167_THERMAL_BASE).WriteTo(&mmio_);
  TempAdcMuxAddr::Get().FromValue(TempSpare::Get(2).addr() + MT8167_THERMAL_BASE).WriteTo(&mmio_);
  TempAdcEnAddr::Get().FromValue(TempSpare::Get(1).addr() + MT8167_THERMAL_BASE).WriteTo(&mmio_);
  TempAdcValidAddr::Get().FromValue(TempSpare::Get(0).addr() + MT8167_THERMAL_BASE).WriteTo(&mmio_);
  TempAdcVoltAddr::Get().FromValue(TempSpare::Get(0).addr() + MT8167_THERMAL_BASE).WriteTo(&mmio_);

  TempRdCtrl::Get().ReadFrom(&mmio_).set_diff(TempRdCtrl::kValidVoltageSame).WriteTo(&mmio_);
  TempAdcValidMask::Get()
      .ReadFrom(&mmio_)
      .set_polarity(TempAdcValidMask::kActiveHigh)
      .set_pos(kAuxAdcBits)
      .WriteTo(&mmio_);
  TempAdcVoltageShift::Get().FromValue(0).WriteTo(&mmio_);
  TempMonCtl0::Get().ReadFrom(&mmio_).enable_all().WriteTo(&mmio_);

  for (uint32_t i = 0; i < kSensorCount; i++) {
    auto msr = TempMsr::Get(i).ReadFrom(&mmio_);
    for (; msr.valid() == 0 || msr.reading() != dummy_temp; msr.ReadFrom(&mmio_)) {
    }
  }

  TempMonCtl0::Get().ReadFrom(&mmio_).disable_all().WriteTo(&mmio_);

  // Set the thermal controller to get temperature readings from the aux ADC.
  TempMonCtl1::Get().ReadFrom(&mmio_).set_period(kThermalPeriod).WriteTo(&mmio_);
  TempMonCtl2::Get()
      .ReadFrom(&mmio_)
      .set_sen_interval(kSenseInterval)
      .set_filt_interval(kFilterInterval)
      .WriteTo(&mmio_);
  TempAhbPoll::Get().FromValue(kAhbPollPeriod).WriteTo(&mmio_);

  TempAdcEn::Get().FromValue(1 << kAuxAdcChannel).WriteTo(&mmio_);
  TempAdcMux::Get().FromValue(1 << kAuxAdcChannel).WriteTo(&mmio_);

  TempPnpMuxAddr::Get().FromValue(kTsCon1Addr).WriteTo(&mmio_);
  TempAdcEnAddr::Get().FromValue(kAuxAdcCon1SetAddr).WriteTo(&mmio_);
  TempAdcMuxAddr::Get().FromValue(kAuxAdcCon1ClrAddr).WriteTo(&mmio_);
  TempAdcValidAddr::Get().FromValue(kAuxAdcDat11Addr).WriteTo(&mmio_);
  TempAdcVoltAddr::Get().FromValue(kAuxAdcDat11Addr).WriteTo(&mmio_);

  TempAdcWriteCtrl::Get().ReadFrom(&mmio_).set_mux_write_en(1).set_pnp_write_en(1).WriteTo(&mmio_);

  TempMonCtl0::Get().ReadFrom(&mmio_).enable_real().WriteTo(&mmio_);

  TempMsrCtl0::Get()
      .ReadFrom(&mmio_)
      .set_msrctl0(TempMsrCtl0::kSample4Drop2)
      .set_msrctl1(TempMsrCtl0::kSample4Drop2)
      .set_msrctl2(TempMsrCtl0::kSample4Drop2)
      .set_msrctl3(TempMsrCtl0::kSample4Drop2)
      .WriteTo(&mmio_);

  return StartThread();
}

void MtkThermal::PmicWrite(uint16_t data, uint32_t addr) {
  while (PmicReadData::Get().ReadFrom(&pmic_mmio_).status() != PmicReadData::kStateIdle) {
  }
  PmicCmd::Get().FromValue(0).set_write(1).set_addr(addr).set_data(data).WriteTo(&pmic_mmio_);
}

float MtkThermal::RawToTemperature(uint32_t raw, uint32_t sensor) {
  int32_t vts = cal2_fuse_.get_vts3();
  if (sensor == 0) {
    vts = cal0_fuse_.get_vts0();
  } else if (sensor == 1) {
    vts = cal0_fuse_.get_vts1();
  } else if (sensor == 2) {
    vts = cal2_fuse_.get_vts2();
  }

  // See misc/mediatek/thermal/mt8167/mtk_ts_cpu.c in the Linux kernel source.
  int32_t gain = 10000 + FixedPoint(cal1_fuse_.get_adc_gain());
  int32_t vts_with_gain = RawWithGain(vts - cal1_fuse_.get_adc_offset(), gain);
  int32_t slope = cal0_fuse_.slope_sign() == 0 ? cal0_fuse_.slope() : -cal0_fuse_.slope();

  int32_t temp_c = ((RawWithGain(raw - cal1_fuse_.get_adc_offset(), gain) - vts_with_gain) * 5) / 6;
  temp_c = (temp_c * 100) / (165 + (cal1_fuse_.id() == 0 ? 0 : slope));
  return static_cast<float>(cal0_fuse_.temp_offset() - temp_c) / 10.0f;
}

uint32_t MtkThermal::TemperatureToRaw(float temp, uint32_t sensor) {
  int32_t vts = cal2_fuse_.get_vts3();
  if (sensor == 0) {
    vts = cal0_fuse_.get_vts0();
  } else if (sensor == 1) {
    vts = cal0_fuse_.get_vts1();
  } else if (sensor == 2) {
    vts = cal2_fuse_.get_vts2();
  }

  int32_t gain = 10000 + FixedPoint(cal1_fuse_.get_adc_gain());
  int32_t vts_with_gain = RawWithGain(vts - cal1_fuse_.get_adc_offset(), gain);
  int32_t slope = cal0_fuse_.slope_sign() == 0 ? cal0_fuse_.slope() : -cal0_fuse_.slope();

  int32_t temp_c = static_cast<int32_t>(cal0_fuse_.temp_offset()) -
                   static_cast<int32_t>(std::round(temp * 10.0f));
  temp_c = (temp_c * (165 + (cal1_fuse_.id() == 0 ? 0 : slope))) / 100;
  return TempWithoutGain(((temp_c * 6) / 5) + vts_with_gain, gain) + cal1_fuse_.get_adc_offset();
}

uint32_t MtkThermal::GetRawHot(float temp) {
  // Find the ADC value corresponding to this temperature for each sensor. ADC values are
  // inversely proportional to temperature, so the maximum represents the lowest temperature
  // required to hit the trip point.

  uint32_t raw_max = 0;
  for (uint32_t i = 0; i < kSensorCount; i++) {
    uint32_t raw = TemperatureToRaw(temp, i);
    if (raw > raw_max) {
      raw_max = raw;
    }
  }

  return raw_max;
}

uint32_t MtkThermal::GetRawCold(float temp) {
  uint32_t raw_min = UINT32_MAX;
  for (uint32_t i = 0; i < kSensorCount; i++) {
    uint32_t raw = TemperatureToRaw(temp, i);
    if (raw < raw_min) {
      raw_min = raw;
    }
  }

  return raw_min;
}

float MtkThermal::ReadTemperatureSensors() {
  uint32_t sensor_values[kSensorCount];
  for (uint32_t i = 0; i < countof(sensor_values); i++) {
    auto msr = TempMsr::Get(i).ReadFrom(&mmio_);
    while (!msr.valid()) {
      msr.ReadFrom(&mmio_);
    }

    sensor_values[i] = msr.reading();
  }

  float temp = RawToTemperature(sensor_values[0], 0);
  for (uint32_t i = 1; i < countof(sensor_values); i++) {
    float sensor_temp = RawToTemperature(sensor_values[i], i);
    if (sensor_temp > temp) {
      temp = sensor_temp;
    }
  }

  return temp;
}

zx_status_t MtkThermal::SetDvfsOpp(uint16_t op_idx) {
  const fuchsia_hardware_thermal_OperatingPoint& opps =
      thermal_info_.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN];
  if (op_idx >= opps.count) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint32_t new_freq = opps.opp[op_idx].freq_hz;
  uint32_t new_volt = opps.opp[op_idx].volt_uv;

  if (new_volt > VprocCon10::kMaxVoltageUv || new_volt < VprocCon10::kMinVoltageUv) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AutoLock lock(&dvfs_lock_);

  auto armpll = ArmPllCon1::Get().ReadFrom(&pll_mmio_);
  uint32_t old_freq = armpll.frequency();

  auto vproc = VprocCon10::Get().FromValue(0).set_voltage(new_volt);
  if (vproc.voltage() != new_volt) {
    // The requested voltage is not a multiple of the voltage step.
    return ZX_ERR_INVALID_ARGS;
  }

  // Switch to a stable clock before changing the ARMPLL frequency.
  auto infra_mux = InfraCfgClkMux::Get().ReadFrom(&infracfg_mmio_);
  infra_mux.set_ifr_mux_sel(InfraCfgClkMux::kIfrClk26M).WriteTo(&infracfg_mmio_);

  armpll.set_frequency(new_freq).WriteTo(&pll_mmio_);

  // Wait for the PLL to stabilize.
  zx::nanosleep(zx::deadline_after(zx::usec(20)));

  if (new_freq > old_freq) {
    PmicWrite(vproc.reg_value(), vproc.reg_addr());
    infra_mux.set_ifr_mux_sel(InfraCfgClkMux::kIfrClkArmPll).WriteTo(&infracfg_mmio_);
  } else {
    infra_mux.set_ifr_mux_sel(InfraCfgClkMux::kIfrClkArmPll).WriteTo(&infracfg_mmio_);
    PmicWrite(vproc.reg_value(), vproc.reg_addr());
  }

  current_op_idx_ = op_idx;

  return ZX_OK;
}

zx_status_t MtkThermal::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t MtkThermal::GetInfo(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr);
}

zx_status_t MtkThermal::GetDeviceInfo(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetDeviceInfo_reply(txn, ZX_OK, &thermal_info_);
}

zx_status_t MtkThermal::GetDvfsInfo(fuchsia_hardware_thermal_PowerDomain power_domain,
                                    fidl_txn_t* txn) {
  if (power_domain != fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
    fuchsia_hardware_thermal_DeviceGetDvfsInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr);
  }

  const fuchsia_hardware_thermal_OperatingPoint* info =
      &thermal_info_.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN];
  return fuchsia_hardware_thermal_DeviceGetDvfsInfo_reply(txn, ZX_OK, info);
}

zx_status_t MtkThermal::GetTemperatureCelsius(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetTemperatureCelsius_reply(txn, ZX_OK,
                                                                    ReadTemperatureSensors());
}

zx_status_t MtkThermal::GetStateChangeEvent(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetStateChangeEvent_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                                  ZX_HANDLE_INVALID);
}

zx_status_t MtkThermal::GetStateChangePort(fidl_txn_t* txn) {
  zx::port dup;
  zx_status_t status = GetPort(&dup);
  return fuchsia_hardware_thermal_DeviceGetStateChangePort_reply(txn, status, dup.release());
}

zx_status_t MtkThermal::SetTripCelsius(uint32_t id, float temp, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceSetTripCelsius_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t MtkThermal::GetDvfsOperatingPoint(fuchsia_hardware_thermal_PowerDomain power_domain,
                                              fidl_txn_t* txn) {
  if (power_domain != fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
    fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
  }

  return fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint_reply(txn, ZX_OK, get_dvfs_opp());
}

zx_status_t MtkThermal::SetDvfsOperatingPoint(uint16_t op_idx,
                                              fuchsia_hardware_thermal_PowerDomain power_domain,
                                              fidl_txn_t* txn) {
  if (power_domain != fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
    fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint_reply(txn, ZX_ERR_NOT_SUPPORTED);
  }

  return fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint_reply(txn, SetDvfsOpp(op_idx));
}

zx_status_t MtkThermal::GetFanLevel(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetFanLevel_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

zx_status_t MtkThermal::SetFanLevel(uint32_t fan_level, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceSetFanLevel_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t MtkThermal::SetTripPoint(size_t trip_pt) {
  zx_port_packet_t packet;
  packet.type = ZX_PKT_TYPE_USER;
  packet.key = trip_pt;

  zx_status_t status = port_.queue(&packet);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Faild to queue packet", __FILE__);
    return status;
  }

  uint32_t raw_hot = 0;
  uint32_t raw_cold = 0xfff;

  if (trip_pt > 0) {
    raw_cold = GetRawCold(thermal_info_.trip_point_info[trip_pt - 1].down_temp_celsius);
  }
  if (trip_pt < thermal_info_.num_trip_points - 1) {
    raw_hot = GetRawHot(thermal_info_.trip_point_info[trip_pt + 1].up_temp_celsius);
  }

  // Update the hot and cold interrupt thresholds for the new trip point.
  TempHotThreshold::Get().ReadFrom(&mmio_).set_threshold(raw_hot).WriteTo(&mmio_);
  TempHotToNormalThreshold::Get().ReadFrom(&mmio_).set_threshold(raw_hot).WriteTo(&mmio_);
  TempColdThreshold::Get().ReadFrom(&mmio_).set_threshold(raw_cold).WriteTo(&mmio_);

  return ZX_OK;
}

int MtkThermal::Thread() {
  const fuchsia_hardware_thermal_ThermalTemperatureInfo* trip_pts = thermal_info_.trip_point_info;

  TempProtCtl::Get().ReadFrom(&mmio_).set_strategy(TempProtCtl::kStrategyMaximum).WriteTo(&mmio_);
  TempProtStage3::Get()
      .FromValue(0)
      .set_threshold(GetRawHot(thermal_info_.critical_temp_celsius))
      .WriteTo(&mmio_);

  float temp = ReadTemperatureSensors();
  TempMsrCtl1::Get().ReadFrom(&mmio_).pause_real().WriteTo(&mmio_);

  // Set the initial trip point based on the current temperature.
  size_t trip_pt = 0;
  for (; trip_pt < thermal_info_.num_trip_points - 1; trip_pt++) {
    if (temp < trip_pts[trip_pt + 1].up_temp_celsius) {
      break;
    }
  }

  size_t last_trip_pt = trip_pt;
  SetTripPoint(trip_pt);

  TempMonInt::Get()
      .ReadFrom(&mmio_)
      .set_hot_en_0(1)
      .set_cold_en_0(1)
      .set_hot_en_1(1)
      .set_cold_en_1(1)
      .set_hot_en_2(1)
      .set_cold_en_2(1)
      .set_stage_3_en(1)
      .WriteTo(&mmio_);

  TempMsrCtl1::Get().ReadFrom(&mmio_).resume_real().WriteTo(&mmio_);

  while (1) {
    zx_status_t status = WaitForInterrupt();
    if (status == ZX_ERR_CANCELED) {
      return thrd_success;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "%s: IRQ wait failed", __FILE__);
      return thrd_error;
    }

    auto int_status = TempMonIntStatus::Get().ReadFrom(&mmio_);

    auto int_enable = TempMonInt::Get().ReadFrom(&mmio_);
    uint32_t int_enable_old = int_enable.reg_value();
    int_enable.set_reg_value(0).WriteTo(&mmio_);

    // Read the current temperature then pause periodic measurements so we don't get out of sync
    // with the hardware.
    temp = ReadTemperatureSensors();
    TempMsrCtl1::Get().ReadFrom(&mmio_).pause_real().WriteTo(&mmio_);

    if (int_status.stage_3()) {
      trip_pt = thermal_info_.num_trip_points - 1;
      if (SetDvfsOpp(0) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to set safe operating point", __FILE__);
        return thrd_error;
      }
    } else if (int_status.hot_0() || int_status.hot_1() || int_status.hot_2()) {
      // Skip to the appropriate trip point for the current temperature.
      for (; trip_pt < thermal_info_.num_trip_points - 1; trip_pt++) {
        if (temp < trip_pts[trip_pt + 1].up_temp_celsius) {
          break;
        }
      }
    } else if (int_status.cold_0() || int_status.cold_1() || int_status.cold_2()) {
      for (; trip_pt > 0; trip_pt--) {
        if (temp > trip_pts[trip_pt - 1].down_temp_celsius) {
          break;
        }
      }
    }

    if (trip_pt != last_trip_pt) {
      SetTripPoint(trip_pt);
    }

    last_trip_pt = trip_pt;

    int_enable.set_reg_value(int_enable_old).WriteTo(&mmio_);
    TempMsrCtl1::Get().ReadFrom(&mmio_).resume_real().WriteTo(&mmio_);
  }

  return thrd_success;
}

zx_status_t MtkThermal::WaitForInterrupt() { return irq_.wait(nullptr); }

zx_status_t MtkThermal::StartThread() {
  return thrd_status_to_zx_status(thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<MtkThermal*>(arg)->Thread(); }, this,
      "mtk-thermal-thread"));
}

zx_status_t MtkThermal::StopThread() {
  irq_.destroy();
  JoinThread();
  return ZX_OK;
}

void MtkThermal::DdkRelease() {
  StopThread();
  delete this;
}

}  // namespace thermal

static constexpr zx_driver_ops_t mtk_thermal_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = thermal::MtkThermal::Create;
  return ops;
}();

ZIRCON_DRIVER(mtk_thermal, mtk_thermal_driver_ops, "zircon", "0.1");
