// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

#include <functional>

#include "magma/magma_common_defs.h"

using gen_pte_t = uint64_t;

using gpu_addr_t = uint64_t;

constexpr gpu_addr_t kInvalidGpuAddr = ~0;

enum CachingType {
  CACHING_NONE,
  CACHING_LLC,
  CACHING_WRITE_THROUGH,
};

enum AddressSpaceType {
  ADDRESS_SPACE_GGTT,   // Global GTT address space
  ADDRESS_SPACE_PPGTT,  // Per Process GTT address space
};

enum EngineCommandStreamerId {
  RENDER_COMMAND_STREAMER,
  VIDEO_COMMAND_STREAMER,
};

enum class ForceWakeDomain { RENDER, GEN9_MEDIA, GEN12_VDBOX0 };

constexpr EngineCommandStreamerId kCommandStreamers[2] = {RENDER_COMMAND_STREAMER,
                                                          VIDEO_COMMAND_STREAMER};

#endif  // TYPES_H
