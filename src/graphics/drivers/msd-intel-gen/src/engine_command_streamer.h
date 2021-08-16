// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ENGINE_COMMAND_STREAMER_H
#define ENGINE_COMMAND_STREAMER_H

#include <memory>
#include <queue>

#include "address_space.h"
#include "gpu_progress.h"
#include "hardware_status_page.h"
#include "magma_util/register_io.h"
#include "magma_util/status.h"
#include "mapped_batch.h"
#include "msd_intel_context.h"
#include "pagetable.h"
#include "render_init_batch.h"
#include "scheduler.h"
#include "sequencer.h"

// See below
class InflightCommandSequence;

class EngineCommandStreamer : public HardwareStatusPage::Owner {
 public:
  class Owner {
   public:
    virtual magma::RegisterIo* register_io() = 0;
    virtual Sequencer* sequencer() = 0;
  };

  EngineCommandStreamer(Owner* owner, EngineCommandStreamerId id, uint32_t mmio_base,
                        std::unique_ptr<GpuMapping> hw_status_page,
                        std::unique_ptr<Scheduler> scheduler);

  virtual ~EngineCommandStreamer() {}

  EngineCommandStreamerId id() const { return id_; }

  const char* Name() const;

  uint32_t mmio_base() const { return mmio_base_; }

  GpuProgress* progress() { return &progress_; }

  GlobalHardwareStatusPage* hardware_status_page() { return &hw_status_page_; }

  uint64_t GetActiveHeadPointer();

  bool IsIdle() { return inflight_command_sequences_.empty(); }

  // Initialize backing store for the given context on this engine command streamer.
  bool InitContext(MsdIntelContext* context) const;

  bool InitContextWorkarounds(MsdIntelContext* context);
  bool InitContextCacheConfig(MsdIntelContext* context);

  void InitHardware();

  bool Reset();

  // Execute the batch immediately.
  bool ExecBatch(std::unique_ptr<MappedBatch> mapped_batch);

  // Submit the batch for eventual execution.
  void SubmitBatch(std::unique_ptr<MappedBatch> batch);

  // Called in response to a context switch interrupt.
  void ContextSwitched();

  // Called in response to a user interrupt.
  void ProcessCompletedCommandBuffers(uint32_t last_completed_sequence);

  // Reset the engine state and kill the current context.
  void ResetCurrentContext();

  // This does not return ownership of the mapped batches so it is not safe
  // to store the result and this method must be called from the device thread
  std::vector<MappedBatch*> GetInflightBatches();

 protected:
  bool SubmitContext(MsdIntelContext* context, uint32_t tail);
  bool UpdateContext(MsdIntelContext* context, uint32_t tail);
  void SubmitExeclists(MsdIntelContext* context);
  void InvalidateTlbs();

  void ScheduleContext();
  bool MoveBatchToInflight(std::unique_ptr<MappedBatch> mapped_batch);

  // On success, returns a sequence number.
  virtual bool WriteBatchToRingBuffer(MappedBatch* mapped_batch, uint32_t* sequence_number_out) = 0;

  bool StartBatchBuffer(MsdIntelContext* context, uint64_t gpu_addr,
                        AddressSpaceType address_space_type);

  // from intel-gfx-prm-osrc-kbl-vol03-gpu_overview.pdf p.5
  static constexpr uint32_t kRenderEngineMmioBase = 0x2000;
  static constexpr uint32_t kVideoEngineMmioBase = 0x12000;

  magma::RegisterIo* register_io() { return owner_->register_io(); }

  Sequencer* sequencer() { return owner_->sequencer(); }

  GpuMapping* hardware_status_page_mapping() { return hw_status_page_mapping_.get(); }

  std::queue<InflightCommandSequence>& inflight_command_sequences() {
    return inflight_command_sequences_;
  }

 private:
  virtual uint32_t GetContextSize() const { return PAGE_SIZE * 2; }

  bool InitContextBuffer(MsdIntelBuffer* context_buffer, Ringbuffer* ringbuffer,
                         AddressSpace* address_space) const;

  // HardwareStatusPage::Owner
  void* hardware_status_page_cpu_addr(EngineCommandStreamerId id) override {
    DASSERT(id == this->id());
    return hw_status_page_cpu_addr_;
  }

  Owner* owner_;
  EngineCommandStreamerId id_;
  uint32_t mmio_base_;
  GpuProgress progress_;
  GlobalHardwareStatusPage hw_status_page_;
  std::unique_ptr<GpuMapping> hw_status_page_mapping_;
  void* hw_status_page_cpu_addr_{};

  std::unique_ptr<Scheduler> scheduler_;
  std::queue<InflightCommandSequence> inflight_command_sequences_;
  bool context_switch_pending_{};

  friend class TestEngineCommandStreamer;
  friend class TestMsdIntelDevice;
};

class InflightCommandSequence {
 public:
  InflightCommandSequence(uint32_t sequence_number, uint32_t ringbuffer_offset,
                          std::unique_ptr<MappedBatch> mapped_batch)
      : sequence_number_(sequence_number),
        ringbuffer_offset_(ringbuffer_offset),
        mapped_batch_(std::move(mapped_batch)) {}

  uint32_t sequence_number() { return sequence_number_; }

  uint32_t ringbuffer_offset() { return ringbuffer_offset_; }

  std::weak_ptr<MsdIntelContext> GetContext() { return mapped_batch_->GetContext(); }

  MappedBatch* mapped_batch() { return mapped_batch_.get(); }

  InflightCommandSequence(InflightCommandSequence&& seq) {
    sequence_number_ = seq.sequence_number_;
    ringbuffer_offset_ = seq.ringbuffer_offset_;
    mapped_batch_ = std::move(seq.mapped_batch_);
  }

 private:
  uint32_t sequence_number_;
  uint32_t ringbuffer_offset_;
  std::unique_ptr<MappedBatch> mapped_batch_;
};

#endif  // ENGINE_COMMAND_STREAMER_H
