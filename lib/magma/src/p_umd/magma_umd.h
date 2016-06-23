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

#ifndef _MAGMA_UMD_H_
#define _MAGMA_UMD_H_

#include <stdint.h>

#include "magma.h"
#include "p_umd/include/magma_arch.h"

// TODO(MA-3)
#define MagmaUmd _drm_intel_bufmgr

struct MagmaUmd {
    MagmaUmd(MagmaArch* arch);

    uint64_t GetDeviceId() { return magma_arch_get_device_id(arch_); }

    drm_intel_bo* AllocTiledBufferObject(const char* name, uint32_t x, uint32_t y,
                                         uint32_t bytes_per_pixel, uint32_t tiling_mode,
                                         uint32_t* stride);

    static MagmaUmd* New(uint32_t gpu_index);

private:
    MagmaArch* arch_;
};

#endif // _MAGMA_UMD_H_
