// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOCK_MAPPED_BATCH_H
#define MOCK_MAPPED_BATCH_H

#include "mapped_batch.h"

class MockMappedBatch : public MappedBatch {
 public:
  MockMappedBatch() {}
  MockMappedBatch(std::weak_ptr<MsdIntelContext> context, gpu_addr_t gpu_addr)
      : context_(context), gpu_addr_(gpu_addr) {}

  std::weak_ptr<MsdIntelContext> GetContext() override { return context_; }
  bool GetGpuAddress(gpu_addr_t* gpu_addr_out) override {
    if (gpu_addr_ == kInvalidGpuAddr)
      return false;
    // Arbitrary
    *gpu_addr_out = gpu_addr_;
    return true;
  }

  void SetSequenceNumber(uint32_t sequence_number) override { sequence_number_ = sequence_number; }
  uint32_t sequence_number() { return sequence_number_; }

  uint32_t GetPipeControlFlags() override { return 0; }
  GpuMapping* GetBatchMapping() override {
    DASSERT(false);
    return nullptr;
  }

 private:
  std::weak_ptr<MsdIntelContext> context_;
  gpu_addr_t gpu_addr_ = kInvalidGpuAddr;
  uint32_t sequence_number_ = Sequencer::kInvalidSequenceNumber;
};

#endif  // MOCK_MAPPED_BATCH_H
