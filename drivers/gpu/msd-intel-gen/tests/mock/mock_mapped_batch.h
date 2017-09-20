// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOCK_MAPPED_BATCH_H
#define MOCK_MAPPED_BATCH_H

#include "mapped_batch.h"

class MockMappedBatch : public MappedBatch {
public:
    std::weak_ptr<MsdIntelContext> GetContext() override
    {
        return std::weak_ptr<MsdIntelContext>();
    }
    bool GetGpuAddress(gpu_addr_t* gpu_addr_out) override { return false; }
    void SetSequenceNumber(uint32_t sequence_number) override {}
    GpuMapping* GetBatchMapping() override { return nullptr; }
};

#endif // MOCK_MAPPED_BATCH_H
