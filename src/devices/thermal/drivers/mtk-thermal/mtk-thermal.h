// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_THERMAL_DRIVERS_MTK_THERMAL_MTK_THERMAL_H_
#define SRC_DEVICES_THERMAL_DRIVERS_MTK_THERMAL_MTK_THERMAL_H_

#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/platform/device.h>
#include <fbl/mutex.h>

#include "mtk-thermal-reg.h"

namespace thermal {

class MtkThermal;
using DeviceType = ddk::Device<MtkThermal, ddk::Messageable>;

class MtkThermal : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_THERMAL> {
 public:
  virtual ~MtkThermal() = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease();

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  // Visible for testing.
  uint16_t get_dvfs_opp() { return current_op_idx_; }

  zx_status_t Init();
  zx_status_t GetPort(zx::port* port) { return port_.duplicate(ZX_RIGHT_SAME_RIGHTS, port); }
  zx_status_t StartThread();
  virtual zx_status_t StopThread();

 protected:
  // Visible for testing.
  MtkThermal(zx_device_t* parent, ddk::MmioBuffer mmio, ddk::MmioBuffer pll_mmio,
             ddk::MmioBuffer pmic_mmio, ddk::MmioBuffer infracfg_mmio,
             const ddk::CompositeProtocolClient& composite, const ddk::PDevProtocolClient& pdev,
             const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_info, zx::port port,
             zx::interrupt irq, TempCalibration0 cal0_fuse, TempCalibration1 cal1_fuse,
             TempCalibration2 cal2_fuse)
      : DeviceType(parent),
        mmio_(std::move(mmio)),
        pll_mmio_(std::move(pll_mmio)),
        pmic_mmio_(std::move(pmic_mmio)),
        infracfg_mmio_(std::move(infracfg_mmio)),
        composite_(composite),
        pdev_(pdev),
        thermal_info_(thermal_info),
        port_(std::move(port)),
        irq_(std::move(irq)),
        cal0_fuse_(cal0_fuse),
        cal1_fuse_(cal1_fuse),
        cal2_fuse_(cal2_fuse) {}

  virtual void PmicWrite(uint16_t data, uint32_t addr);

  virtual float ReadTemperatureSensors();

  virtual zx_status_t SetDvfsOpp(uint16_t op_idx);

  virtual zx_status_t SetTripPoint(size_t trip_pt);

  virtual zx_status_t WaitForInterrupt();

  int JoinThread() { return thrd_join(thread_, nullptr); }

  ddk::MmioBuffer mmio_;
  ddk::MmioBuffer pll_mmio_;
  ddk::MmioBuffer pmic_mmio_;
  ddk::MmioBuffer infracfg_mmio_;

 private:
  zx_status_t GetInfo(fidl_txn_t* txn);
  zx_status_t GetDeviceInfo(fidl_txn_t* txn);
  zx_status_t GetDvfsInfo(fuchsia_hardware_thermal_PowerDomain power_domain, fidl_txn_t* txn);
  zx_status_t GetTemperatureCelsius(fidl_txn_t* txn);
  zx_status_t GetStateChangeEvent(fidl_txn_t* txn);
  zx_status_t GetStateChangePort(fidl_txn_t* txn);
  zx_status_t SetTripCelsius(uint32_t id, float temp, fidl_txn_t* txn);
  zx_status_t GetDvfsOperatingPoint(fuchsia_hardware_thermal_PowerDomain power_domain,
                                    fidl_txn_t* txn);
  zx_status_t SetDvfsOperatingPoint(uint16_t op_idx,
                                    fuchsia_hardware_thermal_PowerDomain power_domain,
                                    fidl_txn_t* txn);
  zx_status_t GetFanLevel(fidl_txn_t* txn);
  zx_status_t SetFanLevel(uint32_t fan_level, fidl_txn_t* txn);

  static constexpr fuchsia_hardware_thermal_Device_ops_t fidl_ops = {
      .GetTemperatureCelsius =
          fidl::Binder<MtkThermal>::BindMember<&MtkThermal::GetTemperatureCelsius>,
      .GetInfo = fidl::Binder<MtkThermal>::BindMember<&MtkThermal::GetInfo>,
      .GetDeviceInfo = fidl::Binder<MtkThermal>::BindMember<&MtkThermal::GetDeviceInfo>,
      .GetDvfsInfo = fidl::Binder<MtkThermal>::BindMember<&MtkThermal::GetDvfsInfo>,
      .GetStateChangeEvent = fidl::Binder<MtkThermal>::BindMember<&MtkThermal::GetStateChangeEvent>,
      .GetStateChangePort = fidl::Binder<MtkThermal>::BindMember<&MtkThermal::GetStateChangePort>,
      .SetTripCelsius = fidl::Binder<MtkThermal>::BindMember<&MtkThermal::SetTripCelsius>,
      .GetDvfsOperatingPoint =
          fidl::Binder<MtkThermal>::BindMember<&MtkThermal::GetDvfsOperatingPoint>,
      .SetDvfsOperatingPoint =
          fidl::Binder<MtkThermal>::BindMember<&MtkThermal::SetDvfsOperatingPoint>,
      .GetFanLevel = fidl::Binder<MtkThermal>::BindMember<&MtkThermal::GetFanLevel>,
      .SetFanLevel = fidl::Binder<MtkThermal>::BindMember<&MtkThermal::SetFanLevel>,
  };

  float RawToTemperature(uint32_t raw, uint32_t sensor);
  uint32_t TemperatureToRaw(float temp, uint32_t sensor);

  uint32_t GetRawHot(float temp);
  uint32_t GetRawCold(float temp);

  int Thread();

  ddk::CompositeProtocolClient composite_;
  ddk::PDevProtocolClient pdev_;
  const fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info_;
  uint16_t current_op_idx_ = 0;
  zx::port port_;
  zx::interrupt irq_;
  thrd_t thread_;
  fbl::Mutex dvfs_lock_;
  const TempCalibration0 cal0_fuse_;
  const TempCalibration1 cal1_fuse_;
  const TempCalibration2 cal2_fuse_;
};

}  // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_MTK_THERMAL_MTK_THERMAL_H_
