// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <soc/aml-s905d2/s905d2-hiu.h>

// PLL Rate tables
#define HHI_PLL_RATE(_rate, _n, _m, _frac, _od) \
  { .rate = _rate, .n = _n, .m = _m, .frac = _frac, .od = _od, }

/* These settings work for hifi, sys, pcie, and gp0 plls
   While it would be possible to dynamically calculate the four components
   to generate a desired rate, it makes more sense at this time to have a table
   of settings for some known needed rates.  The documentation fo the Amlogic
   plls is somewhat thin and by using the tables we will have known tested good
   rates to choose from.
   fout = 24MHz*m/(n*(1 << od))
*/
static const hhi_pll_rate_t s905d2_hiu_pll_rates[] = {
    HHI_PLL_RATE(768000000, 1, 128, 0, 2),  /*DCO=3072M*/
    HHI_PLL_RATE(846000000, 1, 141, 0, 2),  /*DCO=3384M*/
    HHI_PLL_RATE(1200000000, 1, 200, 0, 2), /*DCO=4800M*/
    HHI_PLL_RATE(1296000000, 1, 216, 0, 2), /*DCO=5184M*/
    HHI_PLL_RATE(1398000000, 1, 233, 0, 2), /*DCO=5592M*/
    HHI_PLL_RATE(1494000000, 1, 249, 0, 2), /*DCO=5976M*/
    HHI_PLL_RATE(1512000000, 1, 126, 0, 1), /*DCO=3024M*/
    HHI_PLL_RATE(1536000000, 1, 128, 0, 1), /*DCO=3072M*/
    HHI_PLL_RATE(1608000000, 1, 134, 0, 1), /*DCO=3216M*/
    HHI_PLL_RATE(1704000000, 1, 142, 0, 1), /*DCO=3408M*/
    HHI_PLL_RATE(1800000000, 1, 150, 0, 1), /*DCO=3600M*/
    HHI_PLL_RATE(1896000000, 1, 158, 0, 1), /*DCO=3792M*/
    HHI_PLL_RATE(1908000000, 1, 159, 0, 1), /*DCO=3816M*/
    HHI_PLL_RATE(3072000000, 1, 128, 0, 0), /*DCO=3072M*/
};

/* Find frequency in the rate table and return pointer to the entry.
   At this point this assumes even integer frequencies. This will be expanded later
   to handle fractional cases.
*/
zx_status_t s905d2_pll_fetch_rate(aml_pll_dev_t* pll_dev, uint64_t freq,
                                  const hhi_pll_rate_t** pll_rate) {
  for (uint32_t i = 0; i < pll_dev->rate_count; i++) {
    if (freq == pll_dev->rate_table[i].rate) {
      *pll_rate = &pll_dev->rate_table[i];
      return ZX_OK;
    }
  }
  *pll_rate = NULL;
  return ZX_ERR_NOT_SUPPORTED;
}

const hhi_pll_rate_t* s905d2_pll_get_rate_table(hhi_plls_t pll_num) {
  switch (pll_num) {
    case GP0_PLL:
    case PCIE_PLL:
    case HIFI_PLL:
    case SYS_PLL:
    case SYS1_PLL:
      return s905d2_hiu_pll_rates;
    default:
      return NULL;
  }
}

size_t s905d2_get_rate_table_count(hhi_plls_t pll_num) {
  switch (pll_num) {
    case GP0_PLL:
    case PCIE_PLL:
    case HIFI_PLL:
    case SYS_PLL:
    case SYS1_PLL:
      return countof(s905d2_hiu_pll_rates);
    default:
      return 0;
  }
}
