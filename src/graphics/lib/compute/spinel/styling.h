// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <stdint.h>
#include <stdbool.h>

#include "spinel_result.h"

//
// STYLING
//

struct spn_styling
{
  struct spn_context      * context;

  struct spn_styling_impl * impl;

  spn_result             (* seal   )(struct spn_styling_impl * const impl);
  spn_result             (* unseal )(struct spn_styling_impl * const impl);
  spn_result             (* release)(struct spn_styling_impl * const impl);

  uint32_t                * extent;

  struct {
    uint32_t                count;
    uint32_t                next;
  } dwords;

  struct {
    uint32_t                count;
  } layers;

  int32_t                   ref_count;
};

//
//
//
