// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAPPED_BATCH_H
#define MAPPED_BATCH_H

#include "gpu_mapping.h"
#include "msd_intel_buffer.h"
#include "platform_bus_mapper.h"
#include "platform_semaphore.h"
#include "sequencer.h"

class MsdIntelContext;

class MappedBatch {
 public:
  enum BatchType {
    UNKNOWN,
    SIMPLE_BATCH,
    COMMAND_BUFFER,
    MAPPING_RELEASE_BATCH,
    PIPELINE_FENCE_BATCH,
  };

  explicit MappedBatch(BatchType type = UNKNOWN) : type_(type) {}

  virtual ~MappedBatch() {}

  virtual std::weak_ptr<MsdIntelContext> GetContext() = 0;
  virtual bool GetGpuAddress(gpu_addr_t* gpu_addr_out) = 0;
  virtual void SetSequenceNumber(uint32_t sequence_number) = 0;
  virtual uint64_t GetBatchBufferId() { return 0; }
  virtual uint32_t GetPipeControlFlags() { return 0; }
  virtual BatchType GetType() { return type_; }
  virtual const GpuMappingView* GetBatchMapping() = 0;

  void scheduled() { scheduled_ = true; }
  bool was_scheduled() { return scheduled_; }

  void set_command_streamer(EngineCommandStreamerId command_streamer) {
    command_streamer_ = command_streamer;
  }

  EngineCommandStreamerId get_command_streamer() { return command_streamer_; }

 private:
  BatchType type_;
  bool scheduled_ = false;
  EngineCommandStreamerId command_streamer_ = RENDER_COMMAND_STREAMER;
};

class SimpleMappedBatch : public MappedBatch {
 public:
  SimpleMappedBatch(std::shared_ptr<MsdIntelContext> context,
                    std::unique_ptr<GpuMapping> batch_buffer_mapping)
      : MappedBatch(SIMPLE_BATCH),
        context_(std::move(context)),
        batch_buffer_mapping_(std::move(batch_buffer_mapping)) {}

  std::weak_ptr<MsdIntelContext> GetContext() override { return context_; }

  bool GetGpuAddress(gpu_addr_t* gpu_addr_out) override {
    *gpu_addr_out = batch_buffer_mapping_->gpu_addr();
    return true;
  }

  void SetSequenceNumber(uint32_t sequence_number) override { sequence_number_ = sequence_number; }

  const GpuMappingView* GetBatchMapping() override { return batch_buffer_mapping_.get(); }

 private:
  std::shared_ptr<MsdIntelContext> context_;
  std::unique_ptr<GpuMapping> batch_buffer_mapping_;
  uint32_t sequence_number_ = Sequencer::kInvalidSequenceNumber;
};

// Has no batch.
class NullBatch : public MappedBatch {
 public:
  explicit NullBatch(BatchType type) : MappedBatch(type) {}

  bool GetGpuAddress(gpu_addr_t* gpu_addr_out) override { return false; }
  void SetSequenceNumber(uint32_t sequence_number) override {}
  const GpuMappingView* GetBatchMapping() override { return nullptr; }
};

// Releases the list of bus mappings when destroyed.
class MappingReleaseBatch : public NullBatch {
 public:
  struct BusMappingsWrapper {
    std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mappings;
  };

  explicit MappingReleaseBatch(std::shared_ptr<BusMappingsWrapper> wrapper)
      : NullBatch(MAPPING_RELEASE_BATCH), wrapper_(std::move(wrapper)) {}

  void SetContext(std::shared_ptr<MsdIntelContext> context) { context_ = std::move(context); }

  std::weak_ptr<MsdIntelContext> GetContext() override { return context_; }

  BusMappingsWrapper* wrapper() { return wrapper_.get(); }

 private:
  std::shared_ptr<MsdIntelContext> context_;
  std::shared_ptr<BusMappingsWrapper> wrapper_;
};

// Signals an event upon completion.
class PipelineFenceBatch : public NullBatch {
 public:
  explicit PipelineFenceBatch(std::shared_ptr<MsdIntelContext> context,
                              std::shared_ptr<magma::PlatformSemaphore> event)
      : NullBatch(PIPELINE_FENCE_BATCH), context_(std::move(context)), event_(std::move(event)) {}

  ~PipelineFenceBatch() override { event_->Signal(); }

  std::weak_ptr<MsdIntelContext> GetContext() override { return context_; }

 private:
  std::shared_ptr<MsdIntelContext> context_;
  std::shared_ptr<magma::PlatformSemaphore> event_;
};

#endif  // MAPPED_BATCH_H
