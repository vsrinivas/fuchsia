// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167.h"

#include <mmio-ptr/fake.h>
#include <zxtest/zxtest.h>

#include "soc/mt8167/mt8167-clk-regs.h"
#include "soc/mt8167/mt8167-hw.h"
#include "zircon/types.h"

// This stubs ensures the power device setup succeeds
__EXPORT zx_status_t device_add_composite(zx_device_t* dev, const char* name,
                                          const composite_device_desc_t* comp_desc) {
  return ZX_OK;
}

namespace board_mt8167 {

class Mt8167Test : public Mt8167, public ddk::PBusProtocol<Mt8167Test> {
 public:
  Mt8167Test() : Mt8167(nullptr) {
    pbus_protocol_t pbus_proto = {.ops = &pbus_protocol_ops_, .ctx = this};
    pbus_ = ddk::PBusProtocolClient(&pbus_proto);
  }

  bool Ok() { return vgp1_enable_called_ && thermal_enable_called_second_; }

  // These stubs ensure the power device setup succeeds.
  zx_status_t PBusDeviceAdd(const pbus_dev_t* dev) { return ZX_OK; }
  zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) { return ZX_OK; }
  zx_status_t PBusRegisterProtocol(uint32_t proto_id, const uint8_t* protocol_buffer,
                                   size_t protocol_size) {
    return ZX_OK;
  }
  zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info) { return ZX_OK; }
  zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info) { return ZX_OK; }
  zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info) { return ZX_OK; }
  zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cb) { return ZX_OK; }
  zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev, const device_fragment_t* fragments_list,
                                     size_t fragments_count, uint32_t t_coresident_device_index) {
    return ZX_OK;
  }

  void TestInitMmPll();

 private:
  zx_status_t Vgp1Enable() override {
    vgp1_enable_called_ = true;
    return ZX_OK;
  }

  zx_status_t Msdc0Init() override { return ZX_OK; }
  zx_status_t Msdc2Init() override { return ZX_OK; }
  zx_status_t SocInit() override { return ZX_OK; }
  zx_status_t SysmemInit() override { return ZX_OK; }
  zx_status_t GpioInit() override { return ZX_OK; }
  zx_status_t GpuInit() override { return ZX_OK; }
  zx_status_t DisplayInit() override { return ZX_OK; }
  zx_status_t I2cInit() override { return ZX_OK; }
  zx_status_t ButtonsInit() override { return ZX_OK; }
  zx_status_t ClkInit() override { return ZX_OK; }
  zx_status_t UsbInit() override { return ZX_OK; }
  zx_status_t ThermalInit() override {
    thermal_enable_called_second_ = vgp1_enable_called_;
    return ZX_OK;
  }
  zx_status_t TouchInit() override { return ZX_OK; }
  zx_status_t BacklightInit() override { return ZX_OK; }
  zx_status_t AudioInit() override { return ZX_OK; }
  bool vgp1_enable_called_ = false;
  bool thermal_enable_called_second_ = false;
};

TEST(Mt8167Test, PmicInitOrder) {
  Mt8167Test dut;
  EXPECT_EQ(0, dut.Thread());
  EXPECT_TRUE(dut.Ok());
}

void Mt8167Test::TestInitMmPll() {
  constexpr size_t kClkRegCount = ZX_PAGE_SIZE / sizeof(uint32_t);
  constexpr size_t kPllRegCount = MT8167_AP_MIXED_SYS_SIZE / sizeof(uint32_t);

  uint32_t clock_reg_array[kClkRegCount] = {};
  uint32_t pll_reg_array[kPllRegCount] = {};
  ddk::MmioBuffer clock_mmio(mmio_buffer_t{.vaddr = FakeMmioPtr(clock_reg_array),
                                           .offset = 0,
                                           .size = ZX_PAGE_SIZE,
                                           .vmo = ZX_HANDLE_INVALID});
  ddk::MmioBuffer pll_mmio(mmio_buffer_t{.vaddr = FakeMmioPtr(pll_reg_array),
                                         .offset = 0,
                                         .size = MT8167_AP_MIXED_SYS_SIZE,
                                         .vmo = ZX_HANDLE_INVALID});

  InitMmPll(&clock_mmio, &pll_mmio);
  EXPECT_EQ(CLK_MUX_SEL0::kMsdc0ClkMmPllDiv3 << 11,
            clock_reg_array[CLK_MUX_SEL0::Get().addr() / 4]);
  MmPllCon1 pll = MmPllCon1::Get().FromValue(pll_reg_array[MmPllCon1::Get().addr() / 4]);
  EXPECT_TRUE(pll.change());
  // Just ignore the fractional part to make this simpler to check.
  EXPECT_EQ(pll.pcw() >> 16, 600'000'000 / 26'000'000);
}

TEST(Mt8167Test, InitMmPll) {
  Mt8167Test dut;
  dut.TestInitMmPll();
}

TEST(Mt8167Test, InitSoc) {
  Mt8167Test dut;
  constexpr size_t kRegCount = (MT8167_SOC_INT_POL + 256) / 4;
  uint32_t regs[kRegCount] = {};
  ddk::MmioBuffer mmio(mmio_buffer_t{
      .vaddr = FakeMmioPtr(regs), .offset = 0, .size = kRegCount * 4, .vmo = ZX_HANDLE_INVALID});
  dut.UpdateRegisters(std::move(mmio));
  EXPECT_EQ(0x0f0f'0f0f, regs[MT8167_SOC_INT_POL / 4]);
  EXPECT_EQ(0x803d'0f0f, regs[MT8167_SOC_INT_POL / 4 + 1]);
  EXPECT_EQ(0x7fff'fbfd, regs[MT8167_SOC_INT_POL / 4 + 5]);
  EXPECT_EQ(0x004e'17fc, regs[MT8167_SOC_INT_POL / 4 + 6]);
}

}  // namespace board_mt8167
