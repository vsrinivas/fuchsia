// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <algorithm>
#include <iterator>

#include <fbl/alloc_checker.h>
#include <soc/aml-a1/a1-hiu.h>
#include <soc/aml-a1/a1-hw.h>

namespace amlogic_clock::a1 {

constexpr uint32_t kA1SyspllSize = 6 * sizeof(uint32_t);
constexpr uint32_t kA1HifipllSize = 6 * sizeof(uint32_t);

// There is no OD in A1 PLL
#define PLL_PARAMS(_rate, _n, _m, _frac) \
  { .rate = _rate, .n = _n, .m = _m, .frac = _frac }

static constexpr hhi_pll_rate_t a1_sys_pll_support_rates[] = {
    PLL_PARAMS(768'000'000, 1, 32, 0),    // 768M
    PLL_PARAMS(792'000'000, 1, 33, 0),    // 792M
    PLL_PARAMS(816'000'000, 1, 34, 0),    // 816M
    PLL_PARAMS(840'000'000, 1, 35, 0),    // 840M
    PLL_PARAMS(864'000'000, 1, 36, 0),    // 864M
    PLL_PARAMS(888'000'000, 1, 37, 0),    // 888M
    PLL_PARAMS(912'000'000, 1, 38, 0),    // 912M
    PLL_PARAMS(936'000'000, 1, 39, 0),    // 936M
    PLL_PARAMS(960'000'000, 1, 40, 0),    // 960M
    PLL_PARAMS(984'000'000, 1, 41, 0),    // 984M
    PLL_PARAMS(1'008'000'000, 1, 42, 0),  // 1008M
    PLL_PARAMS(1'032'000'000, 1, 43, 0),  // 1032M
    PLL_PARAMS(1'056'000'000, 1, 44, 0),  // 1056M
    PLL_PARAMS(1'080'000'000, 1, 45, 0),  // 1080M
    PLL_PARAMS(1'104'000'000, 1, 46, 0),  // 1104M
    PLL_PARAMS(1'128'000'000, 1, 47, 0),  // 1128M
    PLL_PARAMS(1'152'000'000, 1, 48, 0),  // 1152M
    PLL_PARAMS(1'176'000'000, 1, 49, 0),  // 1176M
    PLL_PARAMS(1'200'000'000, 1, 50, 0),  // 1200M
    PLL_PARAMS(1'224'000'000, 1, 51, 0),  // 1224M
    PLL_PARAMS(1'248'000'000, 1, 52, 0),  // 1248M
    PLL_PARAMS(1'272'000'000, 1, 53, 0),  // 1272M
    PLL_PARAMS(1'296'000'000, 1, 54, 0),  // 1296M
    PLL_PARAMS(1'320'000'000, 1, 55, 0),  // 1320M
    PLL_PARAMS(1'344'000'000, 1, 56, 0),  // 1344M
    PLL_PARAMS(1'368'000'000, 1, 57, 0),  // 1368M
    PLL_PARAMS(1'392'000'000, 1, 58, 0),  // 1392M
    PLL_PARAMS(1'416'000'000, 1, 59, 0),  // 1416M
    PLL_PARAMS(1'440'000'000, 1, 60, 0),  // 1440M
    PLL_PARAMS(1'464'000'000, 1, 61, 0),  // 1464M
    PLL_PARAMS(1'488'000'000, 1, 62, 0),  // 1488M
    PLL_PARAMS(1'512'000'000, 1, 63, 0),  // 1512M
    PLL_PARAMS(1'536'000'000, 1, 64, 0),  // 1536M
};

static constexpr hhi_pll_rate_t a1_hifi_pll_support_rates[] = {
    PLL_PARAMS(614'400'000, 5, 128, 0),  // 614.4M
};

#undef PLL_PARAMS

static constexpr struct reg_sequence a1_syspll_default[] = {
    {.reg_offset = 0x1 << 2, .def = 0x01800000},
    {.reg_offset = 0x2 << 2, .def = 0x00001100},
    {.reg_offset = 0x3 << 2, .def = 0x10022300},
    {.reg_offset = 0x4 << 2, .def = 0x00300000},
    {.reg_offset = 0x0 << 2, .def = 0x01f18440},
    {.reg_offset = 0x0 << 2, .def = 0x11f18440, .delay_us = 10},
    {.reg_offset = 0x0 << 2, .def = 0x15f18440, .delay_us = 40},
    {.reg_offset = 0x2 << 2, .def = 0x00001140},
    {.reg_offset = 0x2 << 2, .def = 0x00001100},
};

static constexpr meson_clk_pll_data_t a1_syspll_rates = {
    .init_regs = a1_syspll_default,
    .init_count = std::size(a1_syspll_default),
};

static constexpr struct reg_sequence a1_hifipll_default[] = {
    {.reg_offset = 0x0 << 2, .def = 0x01f19480, .delay_us = 10},
    {.reg_offset = 0x0 << 2, .def = 0x11f19480},
    {.reg_offset = 0x1 << 2, .def = 0x01800000},
    {.reg_offset = 0x2 << 2, .def = 0x00001100},
    {.reg_offset = 0x3 << 2, .def = 0x10022200},
    {.reg_offset = 0x4 << 2, .def = 0x00301000, .delay_us = 10},
    {.reg_offset = 0x0 << 2, .def = 0x15f11480, .delay_us = 10},
};

static constexpr meson_clk_pll_data_t a1_hifipll_rates = {
    .init_regs = a1_hifipll_default,
    .init_count = std::size(a1_hifipll_default),
    .repeatedly_toggling = true,
};

std::unique_ptr<AmlMesonPllDevice> CreatePllDevice(fdf::MmioBuffer* mmio, const uint32_t pll_num) {
  switch (pll_num) {
    case a1::SYS_PLL:
      return AmlA1PllDevice::Create(
          mmio->View(A1_ANACTRL_SYSPLL_CTRL0, kA1SyspllSize), &a1_syspll_rates,
          cpp20::span(a1_sys_pll_support_rates, std::size(a1_sys_pll_support_rates)));
    case a1::HIFI_PLL:
      return AmlA1PllDevice::Create(
          mmio->View(A1_ANACTRL_HIFIPLL_CTRL0, kA1HifipllSize), &a1_hifipll_rates,
          cpp20::span(a1_hifi_pll_support_rates, std::size(a1_hifi_pll_support_rates)));
    default:
      ZX_ASSERT_MSG(0, "Not supported");
      break;
  }

  ZX_ASSERT(0);
  return nullptr;
}

std::unique_ptr<AmlMesonPllDevice> AmlA1PllDevice::Create(
    fdf::MmioView view, const meson_clk_pll_data_t* data,
    const cpp20::span<const hhi_pll_rate_t> rates_table) {
  fbl::AllocChecker ac;

  auto dev =
      std::unique_ptr<AmlA1PllDevice>(new (&ac) AmlA1PllDevice(std::move(view), data, rates_table));
  if (!ac.check()) {
    return nullptr;
  }

  ZX_ASSERT(dev->Initialize() == ZX_OK);

  return dev;
}

}  // namespace amlogic_clock::a1
