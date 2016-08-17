// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include "msd_intel_buffer.h"
#include <memory>

class Ringbuffer {
public:
    static std::unique_ptr<Ringbuffer> Create();

    uint64_t size() { return buffer_->platform_buffer()->size(); }

    MsdIntelBuffer* buffer() { return buffer_.get(); }

    static const uint32_t kRingbufferSize = 32 * PAGE_SIZE;

private:
    Ringbuffer(std::unique_ptr<MsdIntelBuffer> buffer);

    std::unique_ptr<MsdIntelBuffer> buffer_;
};

#endif // RINGBUFFER_H
