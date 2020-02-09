// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_H
#define GPU_MAPPING_H

#include <magma_util/gpu_mapping.h>

#include "msd_intel_buffer.h"

using GpuMappingView = magma::GpuMappingView<MsdIntelBuffer>;
using GpuMapping = magma::GpuMapping<MsdIntelBuffer>;

#endif  // GPU_MAPPING_H
