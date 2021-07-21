// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGMA_INTEL_GEN_DEFS_H
#define MAGMA_INTEL_GEN_DEFS_H

// TODO(fxbug.dev/80901) - migrate msd_intel_gen_query.h into this file
#include "msd_intel_gen_query.h"

enum MagmaIntelGenCommandBufferFlags {
  kMagmaIntelGenCommandBufferForRender = MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0,
  kMagmaIntelGenCommandBufferForVideo = MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0 << 1,
};

#endif  // MSD_INTEL_GEN_QUERY_H
