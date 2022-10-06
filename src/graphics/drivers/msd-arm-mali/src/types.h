// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

#include <functional>

#include "magma/magma_common_defs.h"

using gpu_addr_t = uint64_t;
using mali_pte_t = uint64_t;

enum AccessFlags {
  kAccessFlagWrite = (1 << 0),
  kAccessFlagRead = (1 << 1),
  kAccessFlagNoExecute = (1 << 2),
  // Inner and outer shareable - cache coherent to CPU.
  kAccessFlagShareBoth = (1 << 3),

  // Inner shareable - cache coherent between shader cores (using L2).
  kAccessFlagShareInner = (1 << 4),
};

#endif  // TYPES_H
