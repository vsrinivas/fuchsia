// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include "aml-fclk.h"

#define FCLK_PLL_RATE(_r, _premux, _postmux, _mux_div) \
  { .rate = (_r), .premux = (_premux), .postmux = (_postmux), .mux_div = (_mux_div), }

/*Fix pll rate table*/
static const aml_fclk_rate_table_t fclk_pll_rate_table[] = {
    FCLK_PLL_RATE(100000000, 1, 1, 9),  FCLK_PLL_RATE(250000000, 1, 1, 3),
    FCLK_PLL_RATE(500000000, 1, 1, 1),  FCLK_PLL_RATE(667000000, 2, 0, 0),
    FCLK_PLL_RATE(1000000000, 1, 0, 0),
};

const aml_fclk_rate_table_t* s905d2_fclk_get_rate_table() { return fclk_pll_rate_table; }

size_t s905d2_fclk_get_rate_table_count() { return countof(fclk_pll_rate_table); }
