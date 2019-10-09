// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_GPU_MSD_VSL_GC_INCLUDE_MAGMA_VSL_GC_TYPES_H_
#define GARNET_DRIVERS_GPU_MSD_VSL_GC_INCLUDE_MAGMA_VSL_GC_TYPES_H_

#include <cstdint>

#include "magma_common_defs.h"

struct magma_vsl_gc_chip_identity {
  uint32_t chip_model;
  uint32_t chip_revision;
  uint32_t chip_date;
  uint32_t stream_count;
  uint32_t pixel_pipes;
  uint32_t resolve_pipes;
  uint32_t instruction_count;
  uint32_t num_constants;
  uint32_t varyings_count;
  uint32_t gpu_core_count;
  uint32_t product_id;
  uint8_t chip_flags;
  uint32_t eco_id;
  uint32_t customer_id;
} __attribute__((packed));

#endif  // GARNET_DRIVERS_GPU_MSD_VSL_GC_INCLUDE_MAGMA_VSL_GC_TYPES_H_
