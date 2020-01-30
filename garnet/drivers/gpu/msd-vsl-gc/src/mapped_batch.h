// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAPPED_BATCH_H
#define MAPPED_BATCH_H

#include "gpu_mapping.h"
#include "magma_util/mapped_batch.h"
#include "platform_buffer.h"
#include "platform_semaphore.h"

class MsdVslContext;

using MappedBatch = magma::MappedBatch<MsdVslContext, MsdVslBuffer>;

// Has no batch.
class NullBatch : public MappedBatch {
 public:
  uint64_t GetGpuAddress() const override { return 0; }
  uint64_t GetLength() const override { return 0; }
  void SetSequenceNumber(uint32_t sequence_number) override {}
  const magma::GpuMappingView<MsdVslBuffer>* GetBatchMapping() const override { return nullptr; }
};

// Signals the semaphores when destroyed.
class EventBatch : public NullBatch {
 public:
  EventBatch(std::shared_ptr<MsdVslContext> context,
             std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores)
      : context_(std::move(context)), signal_semaphores_(std::move(signal_semaphores)) {}

  ~EventBatch() {
    for (auto& semaphore : signal_semaphores_) {
      semaphore->Signal();
    }
  }

  std::weak_ptr<MsdVslContext> GetContext() const override { return context_; }

 private:
  std::shared_ptr<MsdVslContext> context_;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;
};

#endif  // MAPPED_BATCH_H
