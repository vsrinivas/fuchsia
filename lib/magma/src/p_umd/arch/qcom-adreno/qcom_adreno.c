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

#include "p_umd/include/magma_arch.h"

#include <stdlib.h>

struct MagmaArch {
    int device_id_;
};

typedef struct MagmaArch QcomAdreno;

QcomAdreno* magma_arch_open(uint32_t gpu_index)
{
    QcomAdreno* arch = (QcomAdreno*)malloc(sizeof(QcomAdreno));
    return arch;
}

void magma_arch_close(QcomAdreno* arch) { free(arch); }

uint64_t magma_arch_get_device_id(QcomAdreno* arch) { return 0; }

bool magma_arch_compute_tiled_buffer_params(QcomAdreno* arch, uint32_t x, uint32_t y,
                                            uint32_t bytes_per_pixel, uint32_t tiling_mode,
                                            uint64_t* size, uint32_t* stride)
{
    return false;
}
