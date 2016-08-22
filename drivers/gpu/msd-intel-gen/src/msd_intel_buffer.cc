// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_buffer.h"
#include "msd.h"

MsdIntelBuffer::MsdIntelBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf)
    : platform_buf_(std::move(platform_buf))
{
    magic_ = kMagic;
}

std::unique_ptr<MsdIntelBuffer> MsdIntelBuffer::Create(msd_platform_buffer* platform_buffer_token)
{
    auto platform_buf = magma::PlatformBuffer::Create(platform_buffer_token);
    if (!platform_buf)
        return DRETP(nullptr,
                     "MsdIntelBuffer::Create: Could not create platform buffer from token");

    return std::unique_ptr<MsdIntelBuffer>(new MsdIntelBuffer(std::move(platform_buf)));
}

std::unique_ptr<MsdIntelBuffer> MsdIntelBuffer::Create(uint64_t size)
{
    auto platform_buf = magma::PlatformBuffer::Create(size);
    if (!platform_buf)
        return DRETP(nullptr, "MsdIntelBuffer::Create: Could not create platform buffer from size");

    return std::unique_ptr<MsdIntelBuffer>(new MsdIntelBuffer(std::move(platform_buf)));
}

//////////////////////////////////////////////////////////////////////////////

msd_buffer* msd_buffer_import(msd_platform_buffer* platform_buf)
{
    auto buffer = MsdIntelBuffer::Create(platform_buf);
    return buffer.release();
}

void msd_buffer_destroy(msd_buffer* buf) { delete MsdIntelBuffer::cast(buf); }
