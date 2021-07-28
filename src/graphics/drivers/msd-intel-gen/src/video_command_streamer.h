// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIDEO_COMMAND_STREAMER_H
#define VIDEO_COMMAND_STREAMER_H

#include <memory>
#include <queue>

#include "engine_command_streamer.h"
#include "mapped_batch.h"
#include "scheduler.h"

// The Video command streamer is similar to the Render command streamer.
// TODO(fxbug.dev/80907) - refactor common scheduling code.
class VideoCommandStreamer : public EngineCommandStreamer {
 public:
  explicit VideoCommandStreamer(EngineCommandStreamer::Owner* owner,
                                std::unique_ptr<GpuMapping> hw_status_page);

  void SubmitBatch(std::unique_ptr<MappedBatch> batch) override;

  bool IsIdle() override { return inflight_command_sequences_.empty(); }

  bool ExecBatch(std::unique_ptr<MappedBatch> mapped_batch) override;

  void ResetCurrentContext() override;

  void ProcessCompletedCommandBuffers(uint32_t last_completed_sequence);
  void ContextSwitched();

 private:
  void ScheduleContext();
  bool MoveBatchToInflight(std::unique_ptr<MappedBatch> mapped_batch);

  std::unique_ptr<Scheduler> scheduler_;
  std::queue<InflightCommandSequence> inflight_command_sequences_;
  bool context_switch_pending_{};
};

#endif
