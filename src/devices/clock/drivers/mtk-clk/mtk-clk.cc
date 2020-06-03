// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-clk.h"

#include <fuchsia/hardware/clock/c/fidl.h>
#include <lib/device-protocol/pdev.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-clk.h>

namespace clk {

struct MtkClkGateRegs {
  const zx_off_t set;
  const zx_off_t clr;
};

constexpr uint32_t kFlagInverted = (1 << 0);

struct MtkClkGate {
  const MtkClkGateRegs regs;
  const uint8_t bit;
  const uint32_t flags;
};

constexpr MtkClkGateRegs kClkGatingCtrl0 = {.set = 0x50, .clr = 0x80};
constexpr MtkClkGateRegs kClkGatingCtrl1 = {.set = 0x54, .clr = 0x84};
constexpr MtkClkGateRegs kClkGatingCtrl2 = {.set = 0x6c, .clr = 0x9c};
constexpr MtkClkGateRegs kClkGatingCtrl8 = {.set = 0xa0, .clr = 0xb0};
constexpr MtkClkGateRegs kClkGatingCtrl9 = {.set = 0xa4, .clr = 0xb4};

// clang-format off
constexpr MtkClkGate kMtkClkGates[] = {
    // kClkGatingCtrl0
    [board_mt8167::kClkPwmMm]          = { .regs = kClkGatingCtrl0, .bit = 0, .flags = 0 },
    [board_mt8167::kClkCamMm]          = { .regs = kClkGatingCtrl0, .bit = 1, .flags = 0 },
    [board_mt8167::kClkMfgMm]          = { .regs = kClkGatingCtrl0, .bit = 2, .flags = 0 },
    [board_mt8167::kClkSpm52m]         = { .regs = kClkGatingCtrl0, .bit = 3, .flags = 0 },
    [board_mt8167::kClkMipi26mDbg]     = { .regs = kClkGatingCtrl0, .bit = 4, .flags = kFlagInverted },
    [board_mt8167::kClkScamMm]         = { .regs = kClkGatingCtrl0, .bit = 5, .flags = 0 },
    [board_mt8167::kClkSmiMm]          = { .regs = kClkGatingCtrl0, .bit = 9, .flags = 0 },

    // kClkGatingCtrl1
    [board_mt8167::kClkThem]           = { .regs = kClkGatingCtrl1, .bit = 1,  .flags = 0 },
    [board_mt8167::kClkApdma]          = { .regs = kClkGatingCtrl1, .bit = 2,  .flags = 0 },
    [board_mt8167::kClkI2c0]           = { .regs = kClkGatingCtrl1, .bit = 3,  .flags = 0 },
    [board_mt8167::kClkI2c1]           = { .regs = kClkGatingCtrl1, .bit = 4,  .flags = 0 },
    [board_mt8167::kClkAuxadc1]        = { .regs = kClkGatingCtrl1, .bit = 5,  .flags = 0 },
    [board_mt8167::kClkNfi]            = { .regs = kClkGatingCtrl1, .bit = 6,  .flags = 0 },
    [board_mt8167::kClkNfiecc]         = { .regs = kClkGatingCtrl1, .bit = 7,  .flags = 0 },
    [board_mt8167::kClkDebugsys]       = { .regs = kClkGatingCtrl1, .bit = 8,  .flags = 0 },
    [board_mt8167::kClkPwm]            = { .regs = kClkGatingCtrl1, .bit = 9,  .flags = 0 },
    [board_mt8167::kClkUart0]          = { .regs = kClkGatingCtrl1, .bit = 10, .flags = 0 },
    [board_mt8167::kClkUart1]          = { .regs = kClkGatingCtrl1, .bit = 11, .flags = 0 },
    [board_mt8167::kClkBtif]           = { .regs = kClkGatingCtrl1, .bit = 12, .flags = 0 },
    [board_mt8167::kClkUsb]            = { .regs = kClkGatingCtrl1, .bit = 13, .flags = 0 },
    [board_mt8167::kClkFlashif26m]     = { .regs = kClkGatingCtrl1, .bit = 14, .flags = 0 },
    [board_mt8167::kClkAuxadc2]        = { .regs = kClkGatingCtrl1, .bit = 15, .flags = 0 },
    [board_mt8167::kClkI2c2]           = { .regs = kClkGatingCtrl1, .bit = 16, .flags = 0 },
    [board_mt8167::kClkMsdc0]          = { .regs = kClkGatingCtrl1, .bit = 17, .flags = 0 },
    [board_mt8167::kClkMsdc1]          = { .regs = kClkGatingCtrl1, .bit = 18, .flags = 0 },
    [board_mt8167::kClkNfi2x]          = { .regs = kClkGatingCtrl1, .bit = 19, .flags = 0 },
    [board_mt8167::kClkPmicwrapAp]     = { .regs = kClkGatingCtrl1, .bit = 20, .flags = 0 },
    [board_mt8167::kClkSej]            = { .regs = kClkGatingCtrl1, .bit = 21, .flags = 0 },
    [board_mt8167::kClkMemslpDlyer]    = { .regs = kClkGatingCtrl1, .bit = 22, .flags = 0 },
    [board_mt8167::kClkSpi]            = { .regs = kClkGatingCtrl1, .bit = 23, .flags = 0 },
    [board_mt8167::kClkApxgpt]         = { .regs = kClkGatingCtrl1, .bit = 24, .flags = 0 },
    [board_mt8167::kClkAudio]          = { .regs = kClkGatingCtrl1, .bit = 25, .flags = 0 },
    [board_mt8167::kClkPmicwrapMd]     = { .regs = kClkGatingCtrl1, .bit = 27, .flags = 0 },
    [board_mt8167::kClkPmicwrapConn]   = { .regs = kClkGatingCtrl1, .bit = 28, .flags = 0 },
    [board_mt8167::kClkPmicwrap26m]    = { .regs = kClkGatingCtrl1, .bit = 29, .flags = 0 },
    [board_mt8167::kClkAuxAdc]         = { .regs = kClkGatingCtrl1, .bit = 30, .flags = 0 },
    [board_mt8167::kClkAuxTp]          = { .regs = kClkGatingCtrl1, .bit = 31, .flags = 0 },

    // kClkGatingCtrl2
    [board_mt8167::kClkMsdc2]          = { .regs = kClkGatingCtrl2, .bit = 0,  .flags = 0 },
    [board_mt8167::kClkRbist]          = { .regs = kClkGatingCtrl2, .bit = 1,  .flags = 0 },
    [board_mt8167::kClkNfiBus]         = { .regs = kClkGatingCtrl2, .bit = 2,  .flags = 0 },
    [board_mt8167::kClkGce]            = { .regs = kClkGatingCtrl2, .bit = 4,  .flags = 0 },
    [board_mt8167::kClkTrng]           = { .regs = kClkGatingCtrl2, .bit = 5,  .flags = 0 },
    [board_mt8167::kClkSej13m]         = { .regs = kClkGatingCtrl2, .bit = 6,  .flags = 0 },
    [board_mt8167::kClkAes]            = { .regs = kClkGatingCtrl2, .bit = 7,  .flags = 0 },
    [board_mt8167::kClkPwmB]           = { .regs = kClkGatingCtrl2, .bit = 8,  .flags = 0 },
    [board_mt8167::kClkPwm1Fb]         = { .regs = kClkGatingCtrl2, .bit = 9,  .flags = 0 },
    [board_mt8167::kClkPwm2Fb]         = { .regs = kClkGatingCtrl2, .bit = 10, .flags = 0 },
    [board_mt8167::kClkPwm3Fb]         = { .regs = kClkGatingCtrl2, .bit = 11, .flags = 0 },
    [board_mt8167::kClkPwm4Fb]         = { .regs = kClkGatingCtrl2, .bit = 12, .flags = 0 },
    [board_mt8167::kClkPwm5Fb]         = { .regs = kClkGatingCtrl2, .bit = 13, .flags = 0 },
    [board_mt8167::kClkUsb1p]          = { .regs = kClkGatingCtrl2, .bit = 14, .flags = 0 },
    [board_mt8167::kClkFlashifFreerun] = { .regs = kClkGatingCtrl2, .bit = 15, .flags = 0 },
    [board_mt8167::kClk26mHdmiSifm]    = { .regs = kClkGatingCtrl2, .bit = 16, .flags = 0 },
    [board_mt8167::kClk26mCec]         = { .regs = kClkGatingCtrl2, .bit = 17, .flags = 0 },
    [board_mt8167::kClk32kCec]         = { .regs = kClkGatingCtrl2, .bit = 18, .flags = 0 },
    [board_mt8167::kClk66mEth]         = { .regs = kClkGatingCtrl2, .bit = 19, .flags = 0 },
    [board_mt8167::kClk133mEth]        = { .regs = kClkGatingCtrl2, .bit = 20, .flags = 0 },
    [board_mt8167::kClkFeth25m]        = { .regs = kClkGatingCtrl2, .bit = 21, .flags = 0 },
    [board_mt8167::kClkFeth50m]        = { .regs = kClkGatingCtrl2, .bit = 22, .flags = 0 },
    [board_mt8167::kClkFlashifAxi]     = { .regs = kClkGatingCtrl2, .bit = 23, .flags = 0 },
    [board_mt8167::kClkUsbif]          = { .regs = kClkGatingCtrl2, .bit = 24, .flags = 0 },
    [board_mt8167::kClkUart2]          = { .regs = kClkGatingCtrl2, .bit = 25, .flags = 0 },
    [board_mt8167::kClkBsi]            = { .regs = kClkGatingCtrl2, .bit = 26, .flags = 0 },
    [board_mt8167::kClkGcpuB]          = { .regs = kClkGatingCtrl2, .bit = 27, .flags = 0 },
    [board_mt8167::kClkMsdc0Infra]     = { .regs = kClkGatingCtrl2, .bit = 28, .flags = kFlagInverted },
    [board_mt8167::kClkMsdc1Infra]     = { .regs = kClkGatingCtrl2, .bit = 29, .flags = kFlagInverted },
    [board_mt8167::kClkMsdc2Infra]     = { .regs = kClkGatingCtrl2, .bit = 30, .flags = kFlagInverted },
    [board_mt8167::kClkUsb78m]         = { .regs = kClkGatingCtrl2, .bit = 31, .flags = 0 },

    // kClkGatingCtrl8
    [board_mt8167::kClkRgSpinor]       = { .regs = kClkGatingCtrl8, .bit = 0,  .flags = 0 },
    [board_mt8167::kClkRgMsdc2]        = { .regs = kClkGatingCtrl8, .bit = 1,  .flags = 0 },
    [board_mt8167::kClkRgEth]          = { .regs = kClkGatingCtrl8, .bit = 2,  .flags = 0 },
    [board_mt8167::kClkRgVdec]         = { .regs = kClkGatingCtrl8, .bit = 3,  .flags = 0 },
    [board_mt8167::kClkRgFdpi0]        = { .regs = kClkGatingCtrl8, .bit = 4,  .flags = 0 },
    [board_mt8167::kClkRgFdpi1]        = { .regs = kClkGatingCtrl8, .bit = 5,  .flags = 0 },
    [board_mt8167::kClkRgAxiMfg]       = { .regs = kClkGatingCtrl8, .bit = 6,  .flags = 0 },
    [board_mt8167::kClkRgSlowMfg]      = { .regs = kClkGatingCtrl8, .bit = 7,  .flags = 0 },
    [board_mt8167::kClkRgAud1]         = { .regs = kClkGatingCtrl8, .bit = 8,  .flags = 0 },
    [board_mt8167::kClkRgAud2]         = { .regs = kClkGatingCtrl8, .bit = 9,  .flags = 0 },
    [board_mt8167::kClkRgAudEngen1]    = { .regs = kClkGatingCtrl8, .bit = 10, .flags = 0 },
    [board_mt8167::kClkRgAudEngen2]    = { .regs = kClkGatingCtrl8, .bit = 11, .flags = 0 },
    [board_mt8167::kClkRgI2c]          = { .regs = kClkGatingCtrl8, .bit = 12, .flags = 0 },
    [board_mt8167::kClkRgPwmInfra]     = { .regs = kClkGatingCtrl8, .bit = 13, .flags = 0 },
    [board_mt8167::kClkRgAudSpdifIn]   = { .regs = kClkGatingCtrl8, .bit = 14, .flags = 0 },
    [board_mt8167::kClkRgUart2]        = { .regs = kClkGatingCtrl8, .bit = 15, .flags = 0 },
    [board_mt8167::kClkRgBsi]          = { .regs = kClkGatingCtrl8, .bit = 16, .flags = 0 },
    [board_mt8167::kClkRgDbgAtclk]     = { .regs = kClkGatingCtrl8, .bit = 17, .flags = 0 },
    [board_mt8167::kClkRgNfiecc]       = { .regs = kClkGatingCtrl8, .bit = 18, .flags = 0 },

    // kClkGatingCtrl9
    [board_mt8167::kClkRgApll1D2En]    = { .regs = kClkGatingCtrl9, .bit = 8,  .flags = kFlagInverted },
    [board_mt8167::kClkRgApll1D4En]    = { .regs = kClkGatingCtrl9, .bit = 9,  .flags = kFlagInverted },
    [board_mt8167::kClkRgApll1D8En]    = { .regs = kClkGatingCtrl9, .bit = 10, .flags = kFlagInverted },
    [board_mt8167::kClkRgApll2D2En]    = { .regs = kClkGatingCtrl9, .bit = 11, .flags = kFlagInverted },
    [board_mt8167::kClkRgApll2D4En]    = { .regs = kClkGatingCtrl9, .bit = 12, .flags = kFlagInverted },
    [board_mt8167::kClkRgApll2D8En]    = { .regs = kClkGatingCtrl9, .bit = 13, .flags = kFlagInverted },
};
// clang-format on

struct clock_info {
  uint32_t idx;
  const char* name;
};

static struct clock_info clks[] = {
    {.idx = 1, .name = "mainpll_div8"},  {.idx = 2, .name = "mainpll_div11"},
    {.idx = 3, .name = "mainpll_div12"}, {.idx = 4, .name = "mainpll_div20"},
    {.idx = 5, .name = "mainpll_div7"},  {.idx = 6, .name = "univpll_div16"},
    {.idx = 7, .name = "univpll_div24"}, {.idx = 8, .name = "nfix2"},
    {.idx = 9, .name = "whpll"},         {.idx = 10, .name = "wpll"},
    {.idx = 11, .name = "26mhz"},        {.idx = 18, .name = "mfg"},
    {.idx = 19, .name = "msdc0"},        {.idx = 20, .name = "msdc1"},
    {.idx = 45, .name = "axi_mfg"},      {.idx = 46, .name = "slow_mfg"},
    {.idx = 47, .name = "aud1"},         {.idx = 48, .name = "aud2"},
    {.idx = 49, .name = "aud engen1"},   {.idx = 50, .name = "aud engen2"},
    {.idx = 67, .name = "mmpll"},        {.idx = 69, .name = "aud1pll"},
    {.idx = 70, .name = "aud2pll"},
};

zx_status_t fidl_clk_measure(void* ctx, uint32_t clk, fidl_txn_t* txn) {
  auto dev = static_cast<MtkClk*>(ctx);
  fuchsia_hardware_clock_FrequencyInfo info;

  dev->ClkMeasure(clk, &info);

  return fuchsia_hardware_clock_DeviceMeasure_reply(txn, &info);
}

zx_status_t fidl_clk_get_count(void* ctx, fidl_txn_t* txn) {
  auto dev = static_cast<MtkClk*>(ctx);

  return fuchsia_hardware_clock_DeviceGetCount_reply(txn, dev->GetClkCount());
}

static const fuchsia_hardware_clock_Device_ops_t fidl_ops_ = {
    .Measure = fidl_clk_measure,
    .GetCount = fidl_clk_get_count,
};

namespace {

class FrequencyMeterControl : public hwreg::RegisterBase<FrequencyMeterControl, uint32_t> {
 public:
  enum {
    kFixClk26Mhz = 0,
    kFixClk32Khz = 2,
  };

  DEF_FIELD(29, 28, ck_div);
  DEF_FIELD(24, 24, fixclk_sel);
  DEF_FIELD(22, 16, monclk_sel);
  DEF_BIT(15, enable);
  DEF_BIT(14, reset);
  DEF_FIELD(11, 0, window);

  static auto Get() { return hwreg::RegisterAddr<FrequencyMeterControl>(0x10); }
};

}  // namespace

zx_status_t MtkClk::Bind() {
  zx_status_t status;
  pbus_protocol_t pbus;
  status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "MtkClk: failed to get ZX_PROTOCOL_PBUS, st = %d", status);
    return status;
  }

  clock_impl_protocol_t clk_proto = {
      .ops = &clock_impl_protocol_ops_,
      .ctx = this,
  };

  status = pbus_register_protocol(&pbus, ZX_PROTOCOL_CLOCK_IMPL, &clk_proto, sizeof(clk_proto));
  if (status != ZX_OK) {
    zxlogf(ERROR, "MtkClk::Create: pbus_register_protocol failed, st = %d", status);
    return status;
  }

  return DdkAdd("mtk-clk");
}

zx_status_t MtkClk::Create(zx_device_t* parent) {
  zx_status_t status;

  pdev_protocol_t pdev_proto;
  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_proto)) != ZX_OK) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available", __FILE__);
    return status;
  }

  ddk::PDev pdev(&pdev_proto);
  std::optional<ddk::MmioBuffer> mmio;
  if ((status = pdev.MapMmio(0, &mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_map_mmio_buffer failed", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<MtkClk> device(new (&ac) MtkClk(parent, *std::move(mmio)));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: MtkClk alloc failed", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Bind()) != ZX_OK) {
    zxlogf(ERROR, "%s: MtkClk bind failed: %d", __FILE__, status);
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t MtkClk::ClockImplEnable(uint32_t index) {
  if (index >= std::size(kMtkClkGates)) {
    return ZX_ERR_INVALID_ARGS;
  }

  const MtkClkGate& gate = kMtkClkGates[index];

  if (gate.flags & kFlagInverted) {
    mmio_.Write32(1 << gate.bit, gate.regs.set);
  } else {
    mmio_.Write32(1 << gate.bit, gate.regs.clr);
  }
  return ZX_OK;
}

zx_status_t MtkClk::ClockImplDisable(uint32_t index) {
  if (index >= std::size(kMtkClkGates)) {
    return ZX_ERR_INVALID_ARGS;
  }

  const MtkClkGate& gate = kMtkClkGates[index];
  if (gate.flags & kFlagInverted) {
    mmio_.Write32(1 << gate.bit, gate.regs.clr);
  } else {
    mmio_.Write32(1 << gate.bit, gate.regs.set);
  }
  return ZX_OK;
}

zx_status_t MtkClk::ClockImplIsEnabled(uint32_t id, bool* out_enabled) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkClk::ClockImplSetRate(uint32_t id, uint64_t hz) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t MtkClk::ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate,
                                                uint64_t* out_best_rate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkClk::ClockImplGetRate(uint32_t id, uint64_t* out_current_rate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkClk::ClockImplSetInput(uint32_t id, uint32_t idx) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t MtkClk::ClockImplGetNumInputs(uint32_t id, uint32_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkClk::ClockImplGetInput(uint32_t id, uint32_t* out) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t MtkClk::ClkMeasure(uint32_t clk, fuchsia_hardware_clock_FrequencyInfo* info) {
  if (clk >= std::size(clks)) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t max_len = sizeof(info->name);
  size_t len = strnlen(clks[clk].name, max_len);
  if (len == max_len) {
    return ZX_ERR_INVALID_ARGS;
  }

  memcpy(info->name, clks[clk].name, len + 1);

  constexpr uint32_t kWindowSize = 512;
  constexpr uint32_t kFixedClockFreqMHz = 26000000 / 1000000;
  FrequencyMeterControl::Get().FromValue(0).set_reset(true).WriteTo(&mmio_);
  FrequencyMeterControl::Get().FromValue(0).set_reset(false).WriteTo(&mmio_);
  auto ctrl = FrequencyMeterControl::Get().FromValue(0);
  ctrl.set_window(kWindowSize - 1).set_monclk_sel(clks[clk].idx);
  ctrl.set_fixclk_sel(FrequencyMeterControl::kFixClk26Mhz).set_enable(true);
  ctrl.WriteTo(&mmio_);

  hw_wmb();

  // Sleep at least kWindowSize ticks of the fixed clock.
  zx_nanosleep(zx_deadline_after(ZX_USEC(30)));

  // Assume it completed calculating.

  constexpr uint32_t kFrequencyMeterReadData = 0x14;
  uint32_t count = mmio_.Read32(kFrequencyMeterReadData);
  info->frequency = (count * kFixedClockFreqMHz) / kWindowSize;
  FrequencyMeterControl::Get().FromValue(0).set_reset(true).WriteTo(&mmio_);
  FrequencyMeterControl::Get().FromValue(0).set_reset(false).WriteTo(&mmio_);
  return ZX_OK;
}

uint32_t MtkClk::GetClkCount() { return static_cast<uint32_t>(std::size(clks)); }

zx_status_t MtkClk::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_clock_Device_dispatch(this, txn, msg, &fidl_ops_);
}

}  // namespace clk

zx_status_t mtk_clk_bind(void* ctx, zx_device_t* parent) { return clk::MtkClk::Create(parent); }

static constexpr zx_driver_ops_t mtk_clk_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = mtk_clk_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(mtk_clk, mtk_clk_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_CLK),
ZIRCON_DRIVER_END(mtk_clk)
