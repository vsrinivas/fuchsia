// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "magma_umd.h"

drm_intel_bo* MagmaUmd::AllocTiledBufferObject(const char* name, uint32_t x, uint32_t y,
                                               uint32_t bytes_per_pixel, uint32_t tiling_mode,
                                               uint32_t* pitch)
{
    uint64_t size;
    uint32_t stride;

    magma_arch_compute_tiled_buffer_params(arch_, x, y, bytes_per_pixel, tiling_mode, &size,
                                           &stride);

    // TODO(MA-4): find an available buffer or allocate one
    return nullptr;
}

MagmaUmd* MagmaUmd::New(uint32_t gpu_index)
{
    auto arch = magma_arch_open(gpu_index);
    if (!arch) {
        return nullptr;
    }
    return new MagmaUmd(arch);
}

MagmaUmd::MagmaUmd(MagmaArch* arch) : arch_(arch) {}
