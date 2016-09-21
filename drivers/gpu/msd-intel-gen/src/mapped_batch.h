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
    virtual MsdIntelContext* GetContext() = 0;
    virtual bool GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* gpu_addr_out) = 0;
};

class SimpleMappedBatch : public MappedBatch {
public:
    SimpleMappedBatch(std::shared_ptr<MsdIntelContext> context,
                      std::shared_ptr<MsdIntelBuffer> batch_buffer)
        : context_(context), batch_buffer_(batch_buffer)
    {
    }

    MsdIntelContext* GetContext() override { return context_.get(); }

    bool GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* gpu_addr_out) override
    {
        return batch_buffer_->GetGpuAddress(address_space_id, gpu_addr_out);
    }

private:
    std::shared_ptr<MsdIntelContext> context_;
    std::shared_ptr<MsdIntelBuffer> batch_buffer_;
};

#endif // MAPPED_BATCH_H
