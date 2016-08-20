// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RENDER_INIT_BATCH_H
#define RENDER_INIT_BATCH_H

#include "address_space.h"
#include "msd_intel_buffer.h"
#include <memory>
#include <stdint.h>

class RenderInitBatch {
public:
    static uint32_t Size();
    static uint32_t RelocationCount();

    bool Init(std::unique_ptr<MsdIntelBuffer> buffer, AddressSpace* address_space);

    bool GetGpuAddress(AddressSpaceId id, gpu_addr_t* addr_out);

private:
    MsdIntelBuffer* buffer() { return buffer_.get(); }

    std::unique_ptr<MsdIntelBuffer> buffer_;

    static const uint32_t relocs_[];
    static const uint32_t batch_[];

    friend class TestRenderInitBatch;
};

#endif // RENDER_INIT_BATCH_H
