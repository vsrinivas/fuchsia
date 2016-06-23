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

#ifndef _MAGMA_ARCH_H_
#define _MAGMA_ARCH_H_

#include "magma_defs.h"
#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct MagmaArch;

struct MagmaArch* magma_arch_open(uint32_t gpu_index);
void magma_arch_close(struct MagmaArch* arch);

// Returns the device id.  0 is an invalid device id.
uint64_t magma_arch_get_device_id(struct MagmaArch* arch);

bool magma_arch_compute_tiled_buffer_params(struct MagmaArch* arch, uint32_t x, uint32_t y,
                                            uint32_t bytes_per_pixel, uint32_t tiling_mode,
                                            uint64_t* size, uint32_t* stride);

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_ARCH_H_ */
