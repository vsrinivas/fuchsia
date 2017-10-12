// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping.h"
#include "msd_arm_buffer.h"

GpuMapping::GpuMapping(uint64_t addr, uint64_t size, uint64_t flags, Owner* owner,
                       std::shared_ptr<MsdArmBuffer> buffer)
    : addr_(addr), size_(size), flags_(flags), owner_(owner), buffer_(buffer)
{
    buffer->AddMapping(this);
}

GpuMapping::~GpuMapping()
{
    auto buffer = buffer_.lock();
    DASSERT(buffer);
    buffer->RemoveMapping(this);
}

MsdArmBuffer* GpuMapping::buffer() const
{
    auto buffer = buffer_.lock();
    DASSERT(buffer);
    return buffer.get();
}
