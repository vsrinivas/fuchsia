// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <algorithm>
#include <iterator>

#include <fbl/alloc_checker.h>
#include <soc/aml-a5/a5-hiu.h>
#include <soc/aml-a5/a5-hw.h>

namespace amlogic_clock::a5 {

constexpr uint32_t kA5HifiPllSize = 8 * sizeof(uint32_t);
constexpr uint32_t kA5MpllSize = 2 * sizeof(uint32_t);

#define HHI_PLL_RATE(_rate, _n, _m, _frac, _od) \
  { .rate = _rate, .n = _n, .m = _m, .frac = _frac, .od = _od, }

static constexpr hhi_pll_rate_t a5_sys_pll_support_rates[] = {
    HHI_PLL_RATE(1'200'000'000, 0, 0, 0, 0),  // padding, unused
};

static constexpr hhi_pll_rate_t a5_hifipll_support_rates[] = {
    HHI_PLL_RATE(768'000'000, 1, 128, 27307, 2),  // 768'000'000 HZ
};
#undef HHI_PLL_RATE

#define HHI_MPLL_RATE(_rate, _n_in, _sdm_in) \
  { .rate = _rate, .n = _n_in, .m = 0, .frac = _sdm_in, .od = 0, }

static constexpr hhi_pll_rate_t a5_mpll_support_rates[] = {
    HHI_MPLL_RATE(491'520'000, 4, 1131),
};
#undef HHI_MPLL_RATE

static constexpr struct reg_sequence a5_hifipll_default[] = {
    {.reg_offset = 0x0 << 2, .def = 0x30020480, .delay_us = 0},
    {.reg_offset = 0x1 << 2, .def = 0x00006aab, .delay_us = 0},
    {.reg_offset = 0x2 << 2, .def = 0x00000000, .delay_us = 0},
    {.reg_offset = 0x3 << 2, .def = 0x6a285c00, .delay_us = 0},
    {.reg_offset = 0x4 << 2, .def = 0x65771290, .delay_us = 0},
    {.reg_offset = 0x5 << 2, .def = 0x39272000, .delay_us = 0},
    {.reg_offset = 0x6 << 2, .def = 0x56540000, .delay_us = 0},  // 768'000'000 HZ
};

static constexpr meson_clk_pll_data_t a5_hifipll_rates = {
    .init_regs = a5_hifipll_default,
    .init_count = std::size(a5_hifipll_default),
};

static constexpr struct reg_sequence a5_mpll_default[] = {
    {.reg_offset = 0x0 << 2, .def = 0x4040046B, .delay_us = 0},  // 491'520'000 HZ
    {.reg_offset = 0x1 << 2, .def = 0x40000033, .delay_us = 0},
};

static constexpr meson_clk_pll_data_t a5_mpll_rates = {
    .init_regs = a5_mpll_default,
    .init_count = std::size(a5_mpll_default),
};

std::unique_ptr<AmlMesonPllDevice> CreatePllDevice(fdf::MmioBuffer* mmio, const uint32_t pll_num) {
  switch (pll_num) {
    case SYS_PLL:
      return AmlA5SysPllDevice::Create(
          cpp20::span(a5_sys_pll_support_rates, std::size(a5_sys_pll_support_rates)));
    case HIFI_PLL:
      return AmlA5HifiPllDevice::Create(
          mmio->View(A5_ANACTRL_HIFIPLL_CTRL0, kA5HifiPllSize), &a5_hifipll_rates,
          cpp20::span(a5_hifipll_support_rates, std::size(a5_hifipll_support_rates)));
    case MPLL0:
      return AmlA5MpllDevice::Create(
          mmio->View(A5_ANACTRL_MPLL_CTRL1, kA5MpllSize), &a5_mpll_rates,
          cpp20::span(a5_mpll_support_rates, std::size(a5_mpll_support_rates)));
    case MPLL1:
      return AmlA5MpllDevice::Create(
          mmio->View(A5_ANACTRL_MPLL_CTRL3, kA5MpllSize), &a5_mpll_rates,
          cpp20::span(a5_mpll_support_rates, std::size(a5_mpll_support_rates)));
    case MPLL2:
      return AmlA5MpllDevice::Create(
          mmio->View(A5_ANACTRL_MPLL_CTRL5, kA5MpllSize), &a5_mpll_rates,
          cpp20::span(a5_mpll_support_rates, std::size(a5_mpll_support_rates)));
    case MPLL3:
      return AmlA5MpllDevice::Create(
          mmio->View(A5_ANACTRL_MPLL_CTRL7, kA5MpllSize), &a5_mpll_rates,
          cpp20::span(a5_mpll_support_rates, std::size(a5_mpll_support_rates)));
    default:
      ZX_ASSERT_MSG(0, "Not supported");
      break;
  }

  ZX_ASSERT(0);
  return nullptr;
}

std::unique_ptr<AmlMesonPllDevice> AmlA5SysPllDevice::Create(
    const cpp20::span<const hhi_pll_rate_t> rates_table) {
  fbl::AllocChecker ac;

  auto dev = std::unique_ptr<AmlA5SysPllDevice>(new (&ac) AmlA5SysPllDevice(rates_table));
  if (!ac.check()) {
    return nullptr;
  }

  ZX_ASSERT(dev->Initialize() == ZX_OK);

  return dev;
}

std::unique_ptr<AmlMesonPllDevice> AmlA5HifiPllDevice::Create(
    fdf::MmioView view, const meson_clk_pll_data_t* data,
    const cpp20::span<const hhi_pll_rate_t> rates_table) {
  fbl::AllocChecker ac;

  auto dev = std::unique_ptr<AmlA5HifiPllDevice>(
      new (&ac) AmlA5HifiPllDevice(std::move(view), data, rates_table));
  if (!ac.check()) {
    return nullptr;
  }

  ZX_ASSERT(dev->Initialize() == ZX_OK);

  return dev;
}

std::unique_ptr<AmlMesonPllDevice> AmlA5MpllDevice::Create(
    fdf::MmioView view, const meson_clk_pll_data_t* data,
    const cpp20::span<const hhi_pll_rate_t> rates_table) {
  fbl::AllocChecker ac;

  auto dev = std::unique_ptr<AmlA5MpllDevice>(
      new (&ac) AmlA5MpllDevice(std::move(view), data, rates_table));
  if (!ac.check()) {
    return nullptr;
  }

  ZX_ASSERT(dev->Initialize() == ZX_OK);

  return dev;
}

}  // namespace amlogic_clock::a5
