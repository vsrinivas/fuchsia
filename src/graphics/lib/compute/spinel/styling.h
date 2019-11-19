// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_STYLING_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_STYLING_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

#include "spinel/spinel_result.h"

//
// STYLING
//

//
// clang-format off
//

struct spn_styling
{
  struct spn_context      * context;

  struct spn_styling_impl * impl;

  spn_result_t             (* seal   )(struct spn_styling_impl * const impl);
  spn_result_t             (* unseal )(struct spn_styling_impl * const impl);
  spn_result_t             (* release)(struct spn_styling_impl * const impl);

  uint32_t                * extent;

  struct {
    uint32_t                count;
  } layers;

  struct {
    uint32_t                count;
    uint32_t                next;
  } dwords;

  int32_t                   ref_count;
};

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_STYLING_H_
