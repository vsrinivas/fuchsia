// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_H
#define GPU_MAPPING_H

#include <platform_buffer.h>

#include <magma_util/gpu_mapping.h>

using GpuMapping = magma::GpuMapping<magma::PlatformBuffer>;

#endif  // GPU_MAPPING_H
