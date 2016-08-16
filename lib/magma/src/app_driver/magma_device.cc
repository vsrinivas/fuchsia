// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_device.h"

MagmaDevice* MagmaDevice::Open(uint32_t device_handle, int batch_size)
{
    auto sys_dev = magma_system_open(device_handle);
    if (!sys_dev) {
        DLOG("magma_system_open failed");
        return nullptr;
    }

    auto bufmgr = new MagmaDevice(sys_dev);
    if (!bufmgr->Init(batch_size)) {
        DLOG("Couldn't init bufmgr");
        delete bufmgr;
        return nullptr;
    }

    return bufmgr;
}

MagmaDevice::MagmaDevice(MagmaSystemConnection* sys_dev) : sys_dev_(sys_dev)
{
    libdrm_ = new LibdrmIntelGen();
    magic_ = kMagic;
}

MagmaDevice::~MagmaDevice()
{
    magma_system_close(sys_dev());
    delete libdrm_;
}

bool MagmaDevice::Init(uint64_t batch_size)
{
    max_relocs_ = LibdrmIntelGen::ComputeMaxRelocs(batch_size);
    return true;
}

MagmaBuffer* MagmaDevice::AllocBufferObject(const char* name, uint64_t size, uint32_t alignment,
                                            uint32_t tiling_mode, uint32_t stride)
{
    auto buffer = new MagmaBuffer(this, name, alignment);
    if (!buffer) {
        DLOG("failed to allocate MagmaBuffer");
        return nullptr;
    }

    if (!buffer->Alloc(size)) {
        DLOG("tiled buffer allocation failed");
        buffer->Decref();
        return nullptr;
    }

    // TODO - pass stride?
    buffer->SetTilingMode(tiling_mode);

    return buffer;
}

bool MagmaDevice::ExecuteBuffer(MagmaBuffer* buffer, int context_id, uint32_t batch_len,
                                uint32_t flags)
{
    DLOG("TODO: ExecuteBuffer");
    return false;
}
