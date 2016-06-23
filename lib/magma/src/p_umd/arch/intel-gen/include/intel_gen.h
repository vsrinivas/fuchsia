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

#ifndef _INTEL_GEN_H_
#define _INTEL_GEN_H_

#include "p_umd/include/magma_arch.h"

typedef struct MagmaArch IntelGen;

// Platform independent support for Intel GEN series.
struct MagmaArch {
public:
    virtual ~MagmaArch(); // Declared as IntelGen

    virtual uint64_t GetPciDeviceId() = 0;

    bool ComputeTiledBufferParams(uint32_t x, uint32_t y, uint32_t cpp, uint32_t tiling_mode,
                                  uint64_t* size, uint32_t* stride);

private:
    void AdjustTileStride(uint32_t* stride, uint32_t tiling_mode);
    void AdjustTileSize(uint64_t* size, uint32_t tiling_mode);
};

#endif // _INTEL_GEN_H_
