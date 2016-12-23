// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAPPED_BATCH_H
#define MAPPED_BATCH_H

#include "gpu_mapping.h"
#include "msd_intel_buffer.h"
#include "sequencer.h"

class MsdIntelContext;

class MappedBatch {
public:
    virtual ~MappedBatch() {}
    virtual MsdIntelContext* GetContext() = 0;
    virtual bool GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* gpu_addr_out) = 0;
    virtual void SetSequenceNumber(uint32_t sequence_number) = 0;
};

class SimpleMappedBatch : public MappedBatch {
public:
    SimpleMappedBatch(std::shared_ptr<MsdIntelContext> context,
                      std::unique_ptr<GpuMapping> batch_buffer_mapping)
        : context_(context), batch_buffer_mapping_(std::move(batch_buffer_mapping))
    {
        batch_buffer_mapping_->buffer()->IncrementInflightCounter();
    }

    ~SimpleMappedBatch()
    {
        batch_buffer_mapping_->buffer()->DecrementInflightCounter();
    }

    MsdIntelContext* GetContext() override { return context_.get(); }

    bool GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* gpu_addr_out) override
    {
        if (batch_buffer_mapping_->address_space_id() != address_space_id)
            return DRETF(false, "invalid address_space_id");
        *gpu_addr_out = batch_buffer_mapping_->gpu_addr();
        return true;
    }

    void SetSequenceNumber(uint32_t sequence_number) override
    {
        sequence_number_ = sequence_number;
    }

private:
    std::shared_ptr<MsdIntelContext> context_;
    std::unique_ptr<GpuMapping> batch_buffer_mapping_;
    uint32_t sequence_number_ = Sequencer::kInvalidSequenceNumber;
};

#endif // MAPPED_BATCH_H
