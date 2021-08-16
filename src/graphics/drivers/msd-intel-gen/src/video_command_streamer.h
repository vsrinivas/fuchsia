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

 private:
  bool WriteBatchToRingBuffer(MappedBatch* mapped_batch, uint32_t* sequence_number_out) override;
};

#endif  // VIDEO_COMMAND_STREAMER_H
