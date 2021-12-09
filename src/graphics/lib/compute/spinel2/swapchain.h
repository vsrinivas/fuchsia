// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_SWAPCHAIN_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_SWAPCHAIN_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

#include "spinel/spinel_result.h"
#include "spinel/spinel_types.h"

//
// SWAPCHAIN
//

// clang-format off
struct spinel_swapchain
{
  struct spinel_swapchain_impl * impl;

  spinel_result_t             (* release)(struct spinel_swapchain_impl * impl);
  spinel_result_t             (* submit )(struct spinel_swapchain_impl * impl,
                                          spinel_swapchain_submit_t const * submit);

  int32_t                        ref_count;
};
// clang-format on

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_SWAPCHAIN_H_
