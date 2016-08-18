// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_buffer.h"
#include "magma_connection.h"

MagmaBuffer::MagmaBuffer(MagmaConnection* connection, const char* name, uint32_t alignment)
    : connection_(connection), refcount_(new BufferRefcount(name, this)), alignment_(alignment)
{
    magic_ = kMagic;

    // Reserve the maximum number of relocations you will possibly need so the
    // vector never gets resized. We don't have any real evidence that resizing
    // the relocation vector introduces any meaningful performance overhead but
    // cstout@ is convinced that it could be bad so we do this to be safe.
    uint32_t max_relocations = connection->batch_size() / sizeof(uint32_t) / 2 - 2;
    relocations_.reserve(max_relocations);
}

MagmaBuffer::~MagmaBuffer()
{
    DLOG("~MagmaBuffer");
    magma_system_free(connection_->sys_connection(), this->handle);

    this->handle = 0xdeadbeef;
    this->size = 0;
}

bool MagmaBuffer::Alloc(uint64_t size)
{
    uint32_t handle;
    if (!magma_system_alloc(connection_->sys_connection(), size, &size, &handle))
        return false;

    this->handle = static_cast<uint32_t>(handle);
    this->size = size;
    this->virt = nullptr;
    this->offset64 = 0;

    return true;
}

void MagmaBuffer::SetTilingMode(uint32_t tiling_mode)
{
    if (magma_system_set_tiling_mode(connection_->sys_connection(), this->handle, tiling_mode))
        tiling_mode_ = tiling_mode;
}

bool MagmaBuffer::Map(bool write)
{
    void* addr;
    if (!magma_system_map(connection_->sys_connection(), this->handle, &addr))
        return false;

    this->virt = addr;

    if (!magma_system_set_domain(connection_->sys_connection(), this->handle, MAGMA_DOMAIN_CPU,
                                 write ? MAGMA_DOMAIN_CPU : 0))
        return false;

    return true;
}

bool MagmaBuffer::Unmap()
{
    if (!magma_system_unmap(connection_->sys_connection(), this->handle, this->virt))
        return false;

    this->virt = nullptr;
    return true;
}

void MagmaBuffer::WaitRendering()
{
    return magma_system_wait_rendering(connection_->sys_connection(), this->handle);
}

void MagmaBuffer::EmitRelocation(uint32_t offset, MagmaBuffer* target, uint32_t target_offset,
                                 uint32_t read_domains_bitfield, uint32_t write_domains_bitfield)
{
    relocations_.emplace_back(offset, target, target_offset, read_domains_bitfield,
                              write_domains_bitfield);
}