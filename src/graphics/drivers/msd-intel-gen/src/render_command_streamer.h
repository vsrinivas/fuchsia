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

 private:
  uint32_t GetContextSize() const override { return PAGE_SIZE * 20; }

  bool WriteBatchToRingBuffer(MappedBatch* mapped_batch, uint32_t* sequence_number_out) override;

  bool PipeControl(MsdIntelContext* context, uint32_t flags, uint32_t* sequence_number);

  friend class TestEngineCommandStreamer;
};

#endif  // RENDER_COMMAND_STREAMER_H
