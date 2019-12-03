// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_H
#define GPU_MAPPING_H

#include <memory>

#include "magma_util/gpu_mapping.h"
#include "msd_vsl_buffer.h"

using GpuMapping = magma::GpuMapping<MsdVslBuffer>;

#endif  // GPU_MAPPING_H
