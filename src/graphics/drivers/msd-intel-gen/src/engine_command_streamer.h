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
#include "magma_util/status.h"
#include "mapped_batch.h"
#include "msd_intel_context.h"
#include "msd_intel_register_io.h"
#include "pagetable.h"
#include "render_init_batch.h"
#include "scheduler.h"
#include "sequencer.h"

// See below
class InflightCommandSequence;
struct RegisterStateHelper;

class EngineCommandStreamer {
 public:
  class Owner {
   public:
    virtual MsdIntelRegisterIo* register_io() = 0;
    virtual Sequencer* sequencer() = 0;
    virtual uint32_t device_id() = 0;
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

  // Returns the head pointer for the context that's active on this engine.
  uint32_t GetRingbufferHeadPointer();

  bool IsIdle() { return inflight_command_sequences_.empty(); }

  // Initialize backing store for the given context on this engine command streamer.
  bool InitContext(MsdIntelContext* context) const;

  // Sets the given context's "indirect context" batch.
  void InitIndirectContext(MsdIntelContext* context, std::shared_ptr<IndirectContextBatch> batch);

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
  void InitRegisterState(RegisterStateHelper& helper, Ringbuffer* ringbuffer,
                         uint64_t ppgtt_pml4_addr) const;

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
  // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol08-command_stream_programming_0.pdf
  // p.30
  static constexpr uint32_t kRenderEngineMmioBase = 0x2000;
  static constexpr uint32_t kVideoEngineMmioBase = 0x12000;
  static constexpr uint32_t kVideoEngineMmioBaseGen12 = 0x1C0000;

  MsdIntelRegisterIo* register_io() { return owner_->register_io(); }

  Sequencer* sequencer() { return owner_->sequencer(); }

  std::queue<InflightCommandSequence>& inflight_command_sequences() {
    return inflight_command_sequences_;
  }

 private:
  virtual uint32_t GetContextSize() const { return PAGE_SIZE * 2; }

  bool InitContextBuffer(MsdIntelBuffer* context_buffer, Ringbuffer* ringbuffer,
                         AddressSpace* address_space) const;

  Owner* owner_;
  EngineCommandStreamerId id_;
  uint32_t mmio_base_;
  GpuProgress progress_;
  GlobalHardwareStatusPage hw_status_page_;
  uint64_t context_status_read_index_ = 0;
  uint32_t hw_context_id_counter_ = 1;

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
