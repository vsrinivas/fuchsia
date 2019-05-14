// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_HOTSORT_GEN_TRANSPOSE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_HOTSORT_GEN_TRANSPOSE_H_

//
//
//

#include <stdint.h>

//
// There must be an even number of rows.  This is enforced elsewhere.
//
// The transpose requires (cols_log2 * rows/2) row-pair blends.
//

void
hsg_transpose(uint32_t const cols_log2,
              uint32_t const rows,
              void (*pfn_blend)(uint32_t const cols_log2,
                                uint32_t const row_ll,  // lower-left
                                uint32_t const row_ur,  // upper-right
                                void *         blend),
              void * blend,
              void (*pfn_remap)(uint32_t const row_from, uint32_t const row_to, void * remap),
              void * remap);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_HOTSORT_GEN_TRANSPOSE_H_
