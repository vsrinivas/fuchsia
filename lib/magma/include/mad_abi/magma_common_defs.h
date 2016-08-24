// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_COMMON_DEFS_H_
#define _MAGMA_COMMON_DEFS_H_

#include <stdint.h>

// TODO: Handle vendor specific tiling
// TODO: ensure these match the magma api
#define MAGMA_TILING_MODE_NONE 0
#define MAGMA_TILING_MODE_INTEL_X 1
#define MAGMA_TILING_MODE_INTEL_Y 2

#define MAGMA_DOMAIN_CPU 0x00000001
#define MAGMA_DOMAIN_GTT 0x00000040

#define BO_ALLOC_FOR_RENDER (1 << 0)

#endif // _MAGMA_COMMON_DEFS_H_
