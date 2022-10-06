// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_INCLUDE_MAGMA_VSI_VIP_TYPES_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_INCLUDE_MAGMA_VSI_VIP_TYPES_H_

#include <cstdint>

#include "magma/magma_common_defs.h"

enum VsiVipCompressionOption : uint8_t {
  kVsiVipCompressionOptionNone = 0x0,
  kVsiVipCompressionOptionColor = 0x1,
  kVsiVipCompressionOptionDepth = 0x2,
  kVsiVipCompressionOptionMsaaColor = 0x4,
  kVsiVipCompressionOptionMsaaDepth = 0x8,
  kVsiVipCompressionOptionDefault = kVsiVipCompressionOptionColor | kVsiVipCompressionOptionDepth |
                                    kVsiVipCompressionOptionMsaaColor |
                                    kVsiVipCompressionOptionMsaaDepth,
};

enum VsiVipSecureMode : uint8_t {
  kVsiVipSecureModeNone = 0,
  kVsiVipSecureModeNormal = 1,
  kVsiVipSecureModeTa = 2,
};

struct magma_vsi_vip_chip_identity {
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

struct magma_vsi_vip_chip_option {
  bool gpu_profiler;
  bool allow_fast_clear;
  bool power_management;
  bool enable_mmu;
  VsiVipCompressionOption compression;
  uint32_t usc_l1_cache_ratio;
  VsiVipSecureMode secure_mode;
} __attribute__((packed));

#endif  // SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_INCLUDE_MAGMA_VSI_VIP_TYPES_H_
