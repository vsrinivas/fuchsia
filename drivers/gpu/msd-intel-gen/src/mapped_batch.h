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

    virtual std::weak_ptr<MsdIntelContext> GetContext() = 0;
    virtual bool GetGpuAddress(gpu_addr_t* gpu_addr_out) = 0;
    virtual void SetSequenceNumber(uint32_t sequence_number) = 0;
    virtual uint32_t GetPipeControlFlags() { return 0; }
    virtual bool IsSimple() { return false; }
    virtual GpuMapping* GetBatchMapping() = 0;

    void scheduled() { scheduled_ = true; }
    bool was_scheduled() { return scheduled_; }

private:
    bool scheduled_ = false;
};

class SimpleMappedBatch : public MappedBatch {
public:
    SimpleMappedBatch(std::shared_ptr<MsdIntelContext> context,
                      std::unique_ptr<GpuMapping> batch_buffer_mapping)
        : context_(context), batch_buffer_mapping_(std::move(batch_buffer_mapping))
    {
    }

    std::weak_ptr<MsdIntelContext> GetContext() override { return context_; }

    bool GetGpuAddress(gpu_addr_t* gpu_addr_out) override
    {
        *gpu_addr_out = batch_buffer_mapping_->gpu_addr();
        return true;
    }

    void SetSequenceNumber(uint32_t sequence_number) override
    {
        sequence_number_ = sequence_number;
    }

    bool IsSimple() override { return true; }

    GpuMapping* GetBatchMapping() override { return batch_buffer_mapping_.get(); }

private:
    std::shared_ptr<MsdIntelContext> context_;
    std::unique_ptr<GpuMapping> batch_buffer_mapping_;
    uint32_t sequence_number_ = Sequencer::kInvalidSequenceNumber;
};

#endif // MAPPED_BATCH_H
