// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_REGS_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace gdc {

// HHI_APICALGDC_CNTL
class GdcClkCntl : public hwreg::RegisterBase<GdcClkCntl, uint32_t> {
 public:
  DEF_FIELD(27, 25, axi_clk_div);
  DEF_BIT(24, axi_clk_en);
  DEF_FIELD(22, 16, axi_clk_sel);
  DEF_FIELD(11, 9, core_clk_div);
  DEF_BIT(8, core_clk_en);
  DEF_FIELD(6, 0, core_clk_sel);
  static auto Get() { return hwreg::RegisterAddr<GdcClkCntl>(0x16C); }
  GdcClkCntl& reset_axi() {
    set_axi_clk_div(0);
    set_axi_clk_en(0);
    set_axi_clk_sel(0);
    return *this;
  }
  GdcClkCntl& reset_core() {
    set_core_clk_div(0);
    set_core_clk_en(0);
    set_core_clk_sel(0);
    return *this;
  }
};

// HHI_MEM_PD_REG0
class GdcMemPowerDomain : public hwreg::RegisterBase<GdcMemPowerDomain, uint32_t> {
 public:
  DEF_FIELD(19, 18, gdc_pd);
  static auto Get() { return hwreg::RegisterAddr<GdcMemPowerDomain>(0x100); }
};

// GDC registers
class Id : public hwreg::RegisterBase<Id, uint32_t> {
 public:
  DEF_FIELD(31, 0, id);
  static auto Get() { return hwreg::RegisterAddr<Id>(0x0); }
};

class ConfigAddr : public hwreg::RegisterBase<ConfigAddr, uint32_t> {
 public:
  DEF_FIELD(31, 0, config_addr);
  static auto Get() { return hwreg::RegisterAddr<ConfigAddr>(0x10); }
};

class ConfigSize : public hwreg::RegisterBase<ConfigSize, uint32_t> {
 public:
  DEF_FIELD(31, 0, config_size);
  static auto Get() { return hwreg::RegisterAddr<ConfigSize>(0x14); }
};

class DataInWidth : public hwreg::RegisterBase<DataInWidth, uint32_t> {
 public:
  DEF_FIELD(15, 0, width);
  static auto Get() { return hwreg::RegisterAddr<DataInWidth>(0x20); }
};

class DataInHeight : public hwreg::RegisterBase<DataInHeight, uint32_t> {
 public:
  DEF_FIELD(15, 0, height);
  static auto Get() { return hwreg::RegisterAddr<DataInHeight>(0x24); }
};

class DataOutWidth : public hwreg::RegisterBase<DataOutWidth, uint32_t> {
 public:
  DEF_FIELD(15, 0, width);
  static auto Get() { return hwreg::RegisterAddr<DataOutWidth>(0x40); }
};

class DataOutHeight : public hwreg::RegisterBase<DataOutHeight, uint32_t> {
 public:
  DEF_FIELD(15, 0, height);
  static auto Get() { return hwreg::RegisterAddr<DataOutHeight>(0x44); }
};

class DataAddr : public hwreg::RegisterBase<DataAddr, uint32_t> {
 public:
  DEF_FIELD(31, 0, addr);
};

class DataOffset : public hwreg::RegisterBase<DataOffset, uint32_t> {
 public:
  DEF_FIELD(31, 0, offset);
};

// Input.
class Data1InAddr : public gdc::DataAddr {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataAddr>(0x28); }
};

class Data2InAddr : public gdc::DataAddr {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataAddr>(0x30); }
};

class Data3InAddr : public gdc::DataAddr {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataAddr>(0x38); }
};

class Data1InOffset : public gdc::DataOffset {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataOffset>(0x2C); }
};

class Data2InOffset : public gdc::DataOffset {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataOffset>(0x34); }
};

class Data3InOffset : public gdc::DataOffset {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataOffset>(0x3C); }
};

// Output.
class Data1OutAddr : public gdc::DataAddr {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataAddr>(0x48); }
};

class Data2OutAddr : public gdc::DataAddr {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataAddr>(0x50); }
};

class Data3OutAddr : public gdc::DataAddr {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataAddr>(0x58); }
};

class Data1OutOffset : public gdc::DataOffset {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataOffset>(0x4C); }
};

class Data2OutOffset : public gdc::DataOffset {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataOffset>(0x54); }
};

class Data3OutOffset : public gdc::DataOffset {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataOffset>(0x5C); }
};

class Status : public hwreg::RegisterBase<Status, uint32_t> {
 public:
  DEF_BIT(0, busy);
  DEF_BIT(1, error);
  DEF_BIT(8, config_error);
  DEF_BIT(9, user_abort);
  DEF_BIT(10, axi_read_error);
  DEF_BIT(11, axi_write_error);
  DEF_BIT(12, unaligned_access);
  DEF_BIT(13, incompatible_config);

  static auto Get() { return hwreg::RegisterAddr<Status>(0x60); };
};

class Config : public hwreg::RegisterBase<Config, uint32_t> {
 public:
  DEF_BIT(0, start);
  DEF_BIT(1, stop);

  static auto Get() { return hwreg::RegisterAddr<Config>(0x64); };
};

}  // namespace gdc

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_REGS_H_
