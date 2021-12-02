// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_HARDWARE_TEST_SDMMC_TEST_DEVICE_CONTROLLER_REGS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_HARDWARE_TEST_SDMMC_TEST_DEVICE_CONTROLLER_REGS_H_

#include <hwreg/bitfields.h>

#include "sdmmc-test-device-controller.h"

namespace sdmmc {

// See //zircon/system/ulib/hwreg-i2c/include/hwreg/i2c.h

template <class DerivedType>
class SdmmcTestDeviceControllerRegisterBase
    : public hwreg::RegisterBase<DerivedType, uint8_t, void> {
 public:
  template <typename T>
  DerivedType& ReadFrom(T* reg_io) = delete;
  template <typename T>
  DerivedType& WriteTo(T* mmio) = delete;

  using RegisterBaseType = hwreg::RegisterBase<DerivedType, uint8_t, void>;

  zx_status_t ReadFrom(SdmmcTestDeviceController& controller) {
    const auto addr = static_cast<uint8_t>(RegisterBaseType::reg_addr());

    zx::status<uint8_t> value = controller.ReadReg(addr);
    if (value.is_error()) {
      return value.error_value();
    }
    RegisterBaseType::set_reg_value(value.value());
    return ZX_OK;
  }

  zx_status_t WriteTo(SdmmcTestDeviceController& controller) {
    const auto addr = static_cast<uint8_t>(RegisterBaseType::reg_addr());
    return controller.WriteReg(addr, RegisterBaseType::reg_value()).status_value();
  }
};

template <class RegType>
class SdmmcTestDeviceControllerRegisterAddr : public hwreg::RegisterAddr<RegType> {
 public:
  static_assert(std::is_base_of<SdmmcTestDeviceControllerRegisterBase<RegType>, RegType>::value,
                "Parameter of SdmmcTestDeviceControllerRegisterAddr<> should derive from "
                "SdmmcTestDeviceControllerRegisterBase");

  template <typename T>
  RegType ReadFrom(T* reg_io) = delete;

  explicit SdmmcTestDeviceControllerRegisterAddr(uint8_t reg_addr)
      : hwreg::RegisterAddr<RegType>(reg_addr) {}
};

class CoreControl : public SdmmcTestDeviceControllerRegisterBase<CoreControl> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<CoreControl>(0x5); }

  DEF_BIT(0, core_enable);
  DEF_BIT(1, error_injection_enable);
  DEF_BIT(7, por_reset);
};

class CoreStatus : public SdmmcTestDeviceControllerRegisterBase<CoreStatus> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<CoreStatus>(0x6); }

  static zx_status_t WaitForInitSuccess(SdmmcTestDeviceController& controller) {
    CoreStatus reg = CoreStatus::Get().FromValue(0);
    while (!reg.init_finished()) {
      if (zx_status_t status = reg.ReadFrom(controller); status != ZX_OK) {
        return status;
      }
    }

    return ZX_OK;
  }

  static zx_status_t WaitForInitFailure(SdmmcTestDeviceController& controller) {
    CoreStatus reg = CoreStatus::Get().FromValue(0);
    while (!reg.init_failed()) {
      if (zx_status_t status = reg.ReadFrom(controller); status != ZX_OK) {
        return status;
      }
    }

    return ZX_OK;
  }

  DEF_BIT(0, init_finished);
  DEF_BIT(1, init_failed);
};

class Ocr2 : public SdmmcTestDeviceControllerRegisterBase<Ocr2> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<Ocr2>(0x7); }
};

class Ocr1 : public SdmmcTestDeviceControllerRegisterBase<Ocr1> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<Ocr1>(0x8); }
};

class Ocr0 : public SdmmcTestDeviceControllerRegisterBase<Ocr0> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<Ocr0>(0x9); }
};

class Rca1 : public SdmmcTestDeviceControllerRegisterBase<Rca1> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<Rca1>(0xa); }
};

class Rca0 : public SdmmcTestDeviceControllerRegisterBase<Rca0> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<Rca0>(0xb); }
};

class CardStatusR1 : public SdmmcTestDeviceControllerRegisterBase<CardStatusR1> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<CardStatusR1>(0xc); }

  DEF_BIT(0, error);
  DEF_BIT(1, illegal_command);
  DEF_BIT(2, com_crc_error);
  DEF_BIT(3, out_of_range);
};

class CardStatusR5 : public SdmmcTestDeviceControllerRegisterBase<CardStatusR5> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<CardStatusR5>(0xd); }
};

class CrcErrorControl : public SdmmcTestDeviceControllerRegisterBase<CrcErrorControl> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<CrcErrorControl>(0xe); }

  DEF_BIT(6, cmd52_crc_error_enable);
};

class Cmd52ErrorControl : public SdmmcTestDeviceControllerRegisterBase<Cmd52ErrorControl> {
 public:
  static auto Get() { return SdmmcTestDeviceControllerRegisterAddr<Cmd52ErrorControl>(0x17); }

  DEF_FIELD(3, 0, transfers_until_crc_error);
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_HARDWARE_TEST_SDMMC_TEST_DEVICE_CONTROLLER_REGS_H_
