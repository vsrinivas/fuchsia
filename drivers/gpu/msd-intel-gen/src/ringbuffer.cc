// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ringbuffer.h"

std::unique_ptr<Ringbuffer> Ringbuffer::Create()
{
    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(kRingbufferSize));
    if (!buffer)
        return DRETP(nullptr, "failed to create MsdIntelBuffer");

    return std::unique_ptr<Ringbuffer>(new Ringbuffer(std::move(buffer)));
}

Ringbuffer::Ringbuffer(std::unique_ptr<MsdIntelBuffer> buffer) : buffer_(std::move(buffer)) {}
