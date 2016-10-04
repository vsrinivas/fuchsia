// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAPPED_BATCH_H
#define MAPPED_BATCH_H

#include "msd_intel_buffer.h"
//#include "msd_intel_context.h"

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
                      std::shared_ptr<MsdIntelBuffer> batch_buffer)
        : context_(context), batch_buffer_(batch_buffer)
    {
    }

    ~SimpleMappedBatch()
    {
        if (batch_buffer_->sequence_number() == sequence_number_)
            batch_buffer_->SetSequenceNumber(Sequencer::kInvalidSequenceNumber);
    }

    MsdIntelContext* GetContext() override { return context_.get(); }

    bool GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* gpu_addr_out) override
    {
        return batch_buffer_->GetGpuAddress(address_space_id, gpu_addr_out);
    }

    void SetSequenceNumber(uint32_t sequence_number) override
    {
        sequence_number_ = sequence_number;
        batch_buffer_->SetSequenceNumber(sequence_number);
    }

private:
    std::shared_ptr<MsdIntelContext> context_;
    std::shared_ptr<MsdIntelBuffer> batch_buffer_;
    uint32_t sequence_number_ = Sequencer::kInvalidSequenceNumber;
};

#endif // MAPPED_BATCH_H
