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

/**************************************************************************
 *
 * Copyright © 2007 Red Hat Inc.
 * Copyright © 2007-2012 Intel Corporation
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 *          Keith Whitwell <keithw-at-tungstengraphics-dot-com>
 *      Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 */

#include "p_umd/arch/intel-gen/include/intel_gen.h"
#include <stdio.h>

// Alignment must be a power of 2
#define ALIGN(value, alignment) (((value) + ((alignment)-1)) & ~((alignment)-1))

MagmaArch::~MagmaArch() {}

/*
 * Round a given stride (bytes) up to the minimum required for X tiling on a
 * given chip.  We use 512 as the minimum to allow for a later tiling
 * change.
 */
void IntelGen::AdjustTileStride(uint32_t* stride, uint32_t tiling_mode)
{
    /* If untiled, then just align it so that we can do rendering
     * to it with the 3D engine.
     */
    if (tiling_mode == MAGMA_TILING_MODE_NONE) {
        *stride = ALIGN(*stride, 64);
        return;
    }

    /* 965 is flexible */
    *stride = ALIGN(*stride, 128);
}

void IntelGen::AdjustTileSize(uint64_t* size, uint32_t tiling_mode)
{
    if (tiling_mode == MAGMA_TILING_MODE_NONE)
        return;

    /* 965+ just need multiples of page size for tiling */
    *size = ALIGN(*size, 4096);
}

bool IntelGen::ComputeTiledBufferParams(uint32_t x, uint32_t y, uint32_t bytes_per_pixel,
                                        uint32_t tiling_mode, uint64_t* size, uint32_t* stride)
{
    uint32_t height_alignment;

    /* If we're tiled, our allocations are in 8 or 32-row blocks,
     * so failure to align our height means that we won't allocate
     * enough pages.
     *
     * If we're untiled, we still have to align to 2 rows high
     * because the data port accesses 2x2 blocks even if the
     * bottom row isn't to be rendered, so failure to align means
     * we could walk off the end of the GTT and fault.  This is
     * documented on 965, and may be the case on older chipsets
     * too so we try to be careful.
     */
    *stride = x * bytes_per_pixel;
    AdjustTileStride(stride, tiling_mode);

    switch (tiling_mode) {
    case MAGMA_TILING_MODE_INTEL_X:
        height_alignment = 8;
        break;
    case MAGMA_TILING_MODE_INTEL_Y:
        height_alignment = 32;
        break;
    default:
        height_alignment = 2;
        break;
    }

    *size = *stride * ALIGN(y, height_alignment);
    AdjustTileSize(size, tiling_mode);

    return true;
}

//////////////////////////////////////////////////////////////////////////////

uint64_t magma_arch_get_device_id(IntelGen* gen) { return gen->GetPciDeviceId(); }

bool magma_arch_compute_tiled_buffer_params(IntelGen* gen, uint32_t x, uint32_t y,
                                            uint32_t bytes_per_pixel, uint32_t tiling_mode,
                                            uint64_t* size, uint32_t* stride)
{
    return gen->ComputeTiledBufferParams(x, y, bytes_per_pixel, tiling_mode, size, stride);
}


IntelGen* magma_arch_open(uint32_t gpu_index)
{
    printf("magma_arch_open UNIMPLEMENTED\n");
    return nullptr;
}

void magma_arch_close(IntelGen* gen)
{
    printf("magma_arch_close UNIMPLEMENTED\n");
}
