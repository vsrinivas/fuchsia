// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_connection.h"

MagmaConnection* MagmaConnection::Open(int32_t fd, int batch_size)
{
    auto sys_connection = magma_system_open(fd);
    if (!sys_connection) {
        DLOG("magma_system_open failed");
        return nullptr;
    }

    auto bufmgr = new MagmaConnection(sys_connection);
    if (!bufmgr->Init(batch_size)) {
        DLOG("Couldn't init bufmgr");
        delete bufmgr;
        return nullptr;
    }

    return bufmgr;
}

MagmaConnection::MagmaConnection(magma_system_connection* sys_connection)
    : sys_connection_(sys_connection)
{
    magic_ = kMagic;
}

MagmaConnection::~MagmaConnection()
{
    magma_system_close(sys_connection());
}

bool MagmaConnection::Init(uint64_t batch_size)
{
    batch_size_ = batch_size;
    return true;
}

MagmaBuffer* MagmaConnection::AllocBufferObject(const char* name, uint64_t size, uint32_t alignment,
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

bool MagmaConnection::ExportBufferObject(MagmaBuffer* buffer, uint32_t* token_out)
{
    magma_system_export(sys_connection_, buffer->handle, token_out);
    return true;
}
