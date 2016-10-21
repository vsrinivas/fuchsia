// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RENDER_INIT_BATCH_H
#define RENDER_INIT_BATCH_H

#include "address_space.h"
#include "gpu_mapping.h"
#include "msd_intel_buffer.h"
#include <memory>
#include <stdint.h>

class RenderInitBatch {
public:
    RenderInitBatch(uint32_t batch_size, const uint32_t batch_array[], uint32_t relocation_count,
                    const uint32_t relocation_array[])
        : batch_size_(batch_size), batch_(batch_array), relocation_count_(relocation_count),
          relocs_(relocation_array)
    {
    }

    uint32_t size() { return batch_size_; }

    std::unique_ptr<GpuMapping> Init(std::unique_ptr<MsdIntelBuffer> buffer,
                                     std::shared_ptr<AddressSpace> address_space);

private:
    AddressSpaceId address_space_id_;

    const uint32_t batch_size_;
    const uint32_t* batch_;

    const uint32_t relocation_count_;
    const uint32_t* relocs_;

    friend class TestRenderInitBatch;
};

class RenderInitBatchGen8 : public RenderInitBatch {
public:
    RenderInitBatchGen8() : RenderInitBatch(batch_size_, batch_, relocation_count_, relocs_) {}

private:
    static const uint32_t batch_size_;
    static const uint32_t batch_[];

    static const uint32_t relocation_count_;
    static const uint32_t relocs_[];
};

class RenderInitBatchGen9 : public RenderInitBatch {
public:
    RenderInitBatchGen9() : RenderInitBatch(batch_size_, batch_, relocation_count_, relocs_) {}

private:
    static const uint32_t batch_size_;
    static const uint32_t batch_[];

    static const uint32_t relocation_count_;
    static const uint32_t relocs_[];
};

#endif // RENDER_INIT_BATCH_H
