// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RENDER_COMMAND_STREAMER_H
#define RENDER_COMMAND_STREAMER_H

#include "engine_command_streamer.h"

class RenderEngineCommandStreamer : public EngineCommandStreamer {
 public:
  explicit RenderEngineCommandStreamer(EngineCommandStreamer::Owner* owner,
                                       std::unique_ptr<GpuMapping> hw_status_page);

  static std::unique_ptr<RenderInitBatch> CreateRenderInitBatch(uint32_t device_id);

  // |address_space| used to map the render init batch.
  bool RenderInit(std::shared_ptr<MsdIntelContext> context,
                  std::unique_ptr<RenderInitBatch> init_batch,
                  std::shared_ptr<AddressSpace> address_space);

  void SubmitBatch(std::unique_ptr<MappedBatch> batch) override;

  void ProcessCompletedCommandBuffers(uint32_t last_completed_sequence);
  void ContextSwitched();

  bool IsIdle() override { return inflight_command_sequences_.empty(); }

  void ResetCurrentContext() override;

  // This does not return ownership of the mapped batches so it is not safe
  // to safe the result and this method must be called from the device thread
  std::vector<MappedBatch*> GetInflightBatches();

 private:
  uint32_t GetContextSize() const override { return PAGE_SIZE * 20; }

  bool ExecBatch(std::unique_ptr<MappedBatch> mapped_batch) override;

  bool MoveBatchToInflight(std::unique_ptr<MappedBatch> mapped_batch);
  bool WriteSequenceNumber(MsdIntelContext* context, uint32_t* sequence_number_out);
  void ScheduleContext();

  bool PipeControl(MsdIntelContext* context, uint32_t flags, uint32_t* sequence_number);

  std::unique_ptr<Scheduler> scheduler_;
  std::queue<InflightCommandSequence> inflight_command_sequences_;
  bool context_switch_pending_{};

  friend class TestEngineCommandStreamer;
};

#endif  // RENDER_COMMAND_STREAMER_H
