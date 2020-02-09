// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSL_GC_INCLUDE_MAGMA_VSL_GC_TYPES_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSL_GC_INCLUDE_MAGMA_VSL_GC_TYPES_H_

#include <cstdint>

#include "magma_common_defs.h"

enum VslGcCompressionOption : uint8_t {
  kVslGcCompressionOptionNone = 0x0,
  kVslGcCompressionOptionColor = 0x1,
  kVslGcCompressionOptionDepth = 0x2,
  kVslGcCompressionOptionMsaaColor = 0x4,
  kVslGcCompressionOptionMsaaDepth = 0x8,
  kVslGcCompressionOptionDefault = kVslGcCompressionOptionColor | kVslGcCompressionOptionDepth |
                                   kVslGcCompressionOptionMsaaColor |
                                   kVslGcCompressionOptionMsaaDepth,
};

enum VslGcSecureMode : uint8_t {
  kVslGcSecureModeNone = 0,
  kVslGcSecureModeNormal = 1,
  kVslGcSecureModeTa = 2,
};

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

struct magma_vsl_gc_chip_option {
  bool gpu_profiler;
  bool allow_fast_clear;
  bool power_management;
  bool enable_mmu;
  VslGcCompressionOption compression;
  uint32_t usc_l1_cache_ratio;
  VslGcSecureMode secure_mode;
} __attribute__((packed));

#endif  // SRC_GRAPHICS_DRIVERS_MSD_VSL_GC_INCLUDE_MAGMA_VSL_GC_TYPES_H_
