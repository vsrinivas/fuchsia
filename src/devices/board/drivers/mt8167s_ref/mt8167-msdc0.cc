// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/mmio/mmio.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-clk-regs.h>
#include <soc/mt8167/mt8167-hw.h>
#include <soc/mt8167/mt8167-sdmmc.h>

#include "mt8167.h"
#include "src/devices/board/drivers/mt8167s_ref/mt8167_bind.h"

namespace {

constexpr uintptr_t kClkBaseAligned =
    fbl::round_down<uintptr_t, uintptr_t>(MT8167_XO_BASE, PAGE_SIZE);
constexpr size_t kClkOffset = MT8167_XO_BASE - kClkBaseAligned;
static_assert(kClkOffset == 0, "Unaligned clock address");
constexpr size_t kClkSizeAligned =
    fbl::round_up<size_t, size_t>(kClkOffset + MT8167_XO_SIZE, PAGE_SIZE);

constexpr uintptr_t kPllBaseAligned =
    fbl::round_down<uintptr_t, uintptr_t>(MT8167_AP_MIXED_SYS_BASE, PAGE_SIZE);
constexpr size_t kPllOffset = MT8167_AP_MIXED_SYS_BASE - kPllBaseAligned;
static_assert(kPllOffset == 0, "Unaligned PLL address");
constexpr size_t kPllSizeAligned =
    fbl::round_up<size_t, size_t>(kPllOffset + MT8167_AP_MIXED_SYS_SIZE, PAGE_SIZE);

// MMPLL is derived from the 26 MHz crystal oscillator.
constexpr uint32_t kMmPllSrcClkFreq = 26000000;

constexpr uint32_t kFifoDepth = 128;
constexpr uint32_t kSrcClkFreq = 200000000;

}  // namespace

namespace board_mt8167 {

void Mt8167::InitMmPll(ddk::MmioBuffer* clk_mmio, ddk::MmioBuffer* pll_mmio) {
  constexpr uint32_t div_value = MmPllCon1::kDiv4;
  constexpr uint32_t src_clk_shift = MmPllCon1::kPcwFracBits + div_value;
  // The MSDC0 clock will be set to MMPLL/3, so multiply by 3 to get 600 MHz.
  // MMPLL is also used to generate the GPU clock.
  constexpr uint64_t pcw =
      (static_cast<uint64_t>(kSrcClkFreq) << src_clk_shift) * 3 / kMmPllSrcClkFreq;
  MmPllCon1::Get()
      .ReadFrom(&(*pll_mmio))
      .set_change(1)
      .set_div(div_value)
      .set_pcw(pcw)
      .WriteTo(&(*pll_mmio));

  CLK_MUX_SEL0::Get()
      .ReadFrom(&(*clk_mmio))
      .set_msdc0_mux_sel(CLK_MUX_SEL0::kMsdc0ClkMmPllDiv3)
      .WriteTo(&(*clk_mmio));
}

zx_status_t Mt8167::Msdc0Init() {
  static const pbus_mmio_t msdc0_mmios[] = {{
      .base = MT8167_MSDC0_BASE,
      .length = MT8167_MSDC0_SIZE,
  }};

  static const pbus_bti_t msdc0_btis[] = {{
      .iommu_index = 0,
      .bti_id = BTI_MSDC0,
  }};

  static const MtkSdmmcConfig msdc0_config = {
      .fifo_depth = kFifoDepth, .src_clk_freq = kSrcClkFreq, .is_sdio = false};

  static const guid_map_t guid_map[] = {
      // Mappings for Android Things paritition names, for mt8167s_ref and cleo.
      {"boot_a", GUID_ZIRCON_A_VALUE},
      {"boot_b", GUID_ZIRCON_B_VALUE},
      {"vbmeta_a", GUID_VBMETA_A_VALUE},
      {"vbmeta_b", GUID_VBMETA_B_VALUE},
      // For now, just give the paver a place to write Zircon-R,
      // even though the bootloader won't support it.
      {"vendor_a", GUID_ZIRCON_R_VALUE},
      // For now, just give the paver a place to write vbmeta-R,
      // even though the bootloader won't support it.
      {"vendor_b", GUID_VBMETA_R_VALUE},
      {"userdata", GUID_FVM_VALUE},
  };
  static_assert(std::size(guid_map) <= DEVICE_METADATA_GUID_MAP_MAX_ENTRIES);

  static const pbus_metadata_t msdc0_metadata[] = {
      {.type = DEVICE_METADATA_PRIVATE,
       .data_buffer = &msdc0_config,
       .data_size = sizeof(msdc0_config)},
      {.type = DEVICE_METADATA_GUID_MAP, .data_buffer = guid_map, .data_size = sizeof(guid_map)}};

  static const pbus_irq_t msdc0_irqs[] = {
      {.irq = MT8167_IRQ_MSDC0, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH}};

  pbus_dev_t msdc0_dev = {};
  msdc0_dev.name = "emmc";
  msdc0_dev.vid = PDEV_VID_MEDIATEK;
  msdc0_dev.did = PDEV_DID_MEDIATEK_MSDC0;
  msdc0_dev.mmio_list = msdc0_mmios;
  msdc0_dev.mmio_count = countof(msdc0_mmios);
  msdc0_dev.bti_list = msdc0_btis;
  msdc0_dev.bti_count = countof(msdc0_btis);
  msdc0_dev.metadata_list = msdc0_metadata;
  msdc0_dev.metadata_count = countof(msdc0_metadata);
  msdc0_dev.irq_list = msdc0_irqs;
  msdc0_dev.irq_count = countof(msdc0_irqs);

  // TODO(bradenkell): Have the clock driver do this once muxing is supported.
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_resource(get_root_resource());
  std::optional<ddk::MmioBuffer> clk_mmio;
  zx_status_t status = ddk::MmioBuffer::Create(kClkBaseAligned, kClkSizeAligned, *root_resource,
                                               ZX_CACHE_POLICY_UNCACHED_DEVICE, &clk_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to set MSDC0 clock: %d", __FUNCTION__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> pll_mmio;
  status = ddk::MmioBuffer::Create(kPllBaseAligned, kPllSizeAligned, *root_resource,
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &pll_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to set MSDC0 clock: %d", __FUNCTION__, status);
    return status;
  }

  InitMmPll(&*clk_mmio, &*pll_mmio);

  static constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  static constexpr zx_bind_inst_t reset_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_MSDC0_RST),
  };
  static const device_fragment_part_t reset_gpio_fragment[] = {
      {std::size(root_match), root_match},
      {std::size(reset_gpio_match), reset_gpio_match},
  };
  static const device_fragment_t fragments[] = {
      {"gpio-reset", std::size(reset_gpio_fragment), reset_gpio_fragment},
  };

  status = pbus_.CompositeDeviceAdd(&msdc0_dev, fragments, std::size(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd MSDC0 failed: %d", __FUNCTION__, status);
  }

  return status;
}

}  // namespace board_mt8167
