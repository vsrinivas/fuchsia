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

#ifndef _MAGMA_DEVICE_H_
#define _MAGMA_DEVICE_H_

#include "magma.h"
#include "magma_system.h"
#include <libdrm_intel_gen.h>

#include <map>
#include <stdint.h>

// Magma is based on intel libdrm.
// LibdrmIntelGen buffers are based on the api exposed MagmaBufferBase.
class MagmaBuffer : public LibdrmIntelGen::Buffer {
public:
    MagmaBuffer(MagmaDevice* device, const char* name, uint32_t align);
    ~MagmaBuffer() override;

    bool Alloc(uint64_t size);
    bool Map(bool write);
    bool Unmap();
    void WaitRendering();

    MagmaDevice* device() { return device_; }

    void SetTilingMode(uint32_t tiling_mode);
    uint32_t tiling_mode() { return tiling_mode_; }

    static MagmaBuffer* cast(drm_intel_bo* buffer) { return static_cast<MagmaBuffer*>(buffer); }

private:
    MagmaDevice* device_;

    uint32_t tiling_mode_ = MAGMA_TILING_MODE_NONE;
};

// Using struct instead of class because of the opaque C type exposed in magma_defs.h
struct MagmaDevice {
public:
    static MagmaDevice* Open(uint32_t device_handle, int batch_size);
    ~MagmaDevice();

    MagmaSystemDevice* sys_dev() { return sys_dev_; }

    uint64_t max_relocs() { return max_relocs_; }
    uint32_t GetDeviceId() { return magma_system_get_device_id(sys_dev_); }

    bool Init(uint64_t batch_size);

    MagmaBufferBase* AllocBufferObject(const char* name, uint64_t size, uint32_t align,
                                       uint32_t tiling_mode, uint32_t stride);

    bool CreateContext(int* context_id)
    {
        return magma_system_create_context(sys_dev_, context_id);
    }

    bool ExecuteBuffer(MagmaBuffer* buffer, int context_id, uint32_t batch_len, uint32_t flags);

private:
    MagmaDevice(MagmaSystemDevice* sys_dev);

    MagmaSystemDevice* sys_dev_;
    LibdrmIntelGen* libdrm_;

    uint64_t max_relocs_{};
};

#endif // _MAGMA_DEVICE_H_
