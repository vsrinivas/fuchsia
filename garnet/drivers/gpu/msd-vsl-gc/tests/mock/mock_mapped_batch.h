// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOCK_MAPPED_BATCH_H
#define MOCK_MAPPED_BATCH_H

#include "garnet/drivers/gpu/msd-vsl-gc/src/msd_vsl_context.h"
#include "magma_util/mapped_batch.h"
#include "platform_buffer.h"
#include "platform_semaphore.h"

class MockMappedBatch : public magma::MappedBatch<MsdVslContext, GpuMapping::BufferType> {
 public:
  MockMappedBatch(uint64_t gpu_addr, uint64_t length,
                  std::shared_ptr<magma::PlatformSemaphore> semaphore)
      : gpu_addr_(gpu_addr), length_(length), semaphore_(semaphore) {}

  explicit MockMappedBatch(std::shared_ptr<magma::PlatformSemaphore> semaphore)
      : MockMappedBatch(0, 0, semaphore) {}

  ~MockMappedBatch() {
    if (semaphore_) {
      semaphore_->Signal();
    }
  }

  std::weak_ptr<MsdVslContext> GetContext() const override {
    return std::weak_ptr<MsdVslContext>();
  }

  uint64_t GetGpuAddress() const override { return gpu_addr_; }
  uint64_t GetLength() const override { return length_; }

  void SetSequenceNumber(uint32_t sequence_number) override {}
  uint64_t GetBatchBufferId() const override { return 0; }

  const magma::GpuMappingView<AddressSpace::Buffer>* GetBatchMapping() const override {
    return nullptr;
  }

 private:
  uint64_t gpu_addr_;
  uint64_t length_;
  std::shared_ptr<magma::PlatformSemaphore> semaphore_;
};

#endif  // MOCK_MAPPED_BATCH_H
