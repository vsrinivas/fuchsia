// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/render/audio_renderer.h"
#include "lib/media/transport/fifo_allocator.h"
#include "lib/media/transport/mapped_shared_buffer.h"

namespace media {

// AudioRenderer that renders audio via FIDL services.
class FidlAudioRenderer
    : public AudioRendererInProc,
      public PayloadAllocator,
      public std::enable_shared_from_this<FidlAudioRenderer> {
 public:
  static std::shared_ptr<FidlAudioRenderer> Create(
      AudioRenderer2Ptr audio_renderer);

  FidlAudioRenderer(AudioRenderer2Ptr audio_renderer);

  ~FidlAudioRenderer() override;

  // AudioRendererInProc implementation.
  void Flush(bool hold_frame) override;

  std::shared_ptr<PayloadAllocator> allocator() override {
    return shared_from_this();
  }

  Demand SupplyPacket(PacketPtr packet) override;

  const std::vector<std::unique_ptr<StreamTypeSet>>& GetSupportedStreamTypes()
      override {
    return supported_stream_types_;
  }

  void SetStreamType(const StreamType& stream_type) override;

  void Prime(fxl::Closure callback) override;

  void SetTimelineFunction(TimelineFunction timeline_function,
                           fxl::Closure callback) override;

  void SetGain(float gain) override;

  // PayloadAllocator implementation:
  void* AllocatePayloadBuffer(size_t size) override;

  void ReleasePayloadBuffer(void* buffer) override;

 private:
  // Returns the current demand.
  Demand current_demand();

  // Converts a pts in |pts_rate_| units to ns.
  int64_t to_ns(int64_t pts) {
    return pts * (TimelineRate::NsPerSecond / pts_rate_);
  }

  // Converts a pts in ns to |pts_rate_| units.
  int64_t from_ns(int64_t pts) {
    return pts * (pts_rate_ / TimelineRate::NsPerSecond);
  }

  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  AudioRenderer2Ptr audio_renderer_;
  MappedSharedBuffer buffer_;
  FifoAllocator allocator_;
  TimelineRate pts_rate_;
  int64_t last_supplied_pts_ = 0;
  fxl::Closure prime_callback_;
  uint32_t bytes_per_frame_;
  bool flushed_ = true;
  int64_t min_lead_time_ns_ = ZX_MSEC(100);
};

}  // namespace media
