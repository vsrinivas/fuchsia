// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_FIDL_AUDIO_RENDERER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_FIDL_AUDIO_RENDERER_H_

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/media_player/render/audio_renderer.h"
#include "lib/media/transport/fifo_allocator.h"
#include "lib/media/transport/mapped_shared_buffer.h"

namespace media_player {

// AudioRenderer that renders audio via FIDL services.
class FidlAudioRenderer
    : public AudioRendererInProc,
      public PayloadAllocator,
      public std::enable_shared_from_this<FidlAudioRenderer> {
 public:
  static std::shared_ptr<FidlAudioRenderer> Create(
      media::AudioRenderer2Ptr audio_renderer);

  FidlAudioRenderer(media::AudioRenderer2Ptr audio_renderer);

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

  void SetTimelineFunction(media::TimelineFunction timeline_function,
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
    return pts * (media::TimelineRate::NsPerSecond / pts_rate_);
  }

  // Converts a pts in ns to |pts_rate_| units.
  int64_t from_ns(int64_t pts) {
    return pts * (pts_rate_ / media::TimelineRate::NsPerSecond);
  }

  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  media::AudioRenderer2Ptr audio_renderer_;
  media::MappedSharedBuffer buffer_;
  media::FifoAllocator allocator_;
  media::TimelineRate pts_rate_;
  int64_t last_supplied_pts_ = 0;
  fxl::Closure prime_callback_;
  uint32_t bytes_per_frame_;
  bool flushed_ = true;
  int64_t min_lead_time_ns_ = ZX_MSEC(100);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_FIDL_AUDIO_RENDERER_H_
