// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_AUDIO_RENDERER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_AUDIO_RENDERER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/thread_checker.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/playback/mediaplayer/metrics/packet_timing_tracker.h"
#include "src/media/playback/mediaplayer/render/audio_renderer.h"

namespace media_player {

// AudioRenderer that renders audio via FIDL services.
//
// This class run single-threaded.
class FidlAudioRenderer : public AudioRenderer {
 public:
  static std::shared_ptr<FidlAudioRenderer> Create(fuchsia::media::AudioRendererPtr audio_renderer);

  FidlAudioRenderer(fuchsia::media::AudioRendererPtr audio_renderer);

  ~FidlAudioRenderer() override;

  // AudioRenderer implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

  void OnInputConnectionReady(size_t input_index) override;

  void FlushInput(bool hold_frame, size_t input_index, fit::closure callback) override;

  void PutInputPacket(PacketPtr packet, size_t input_index) override;

  const std::vector<std::unique_ptr<StreamTypeSet>>& GetSupportedStreamTypes() override {
    return supported_stream_types_;
  }

  void SetStreamType(const StreamType& stream_type) override;

  void Prime(fit::closure callback) override;

  void SetTimelineFunction(media::TimelineFunction timeline_function,
                           fit::closure callback) override;

  void BindGainControl(
      fidl::InterfaceRequest<fuchsia::media::audio::GainControl> gain_control_request) override;

 protected:
  // Renderer overrides.
  void OnTimelineTransition() override;

 private:
  FIT_DECLARE_THREAD_CHECKER(thread_checker_);

  // Determines if more packets are needed.
  bool NeedMorePackets();

  // Signals current demand via the stage's |RequestInputPacket| if we need
  // more packets. Return value indicates whether an input packet was requested.
  bool SignalCurrentDemand();

  // Converts a pts in |pts_rate_| units to ns.
  int64_t to_ns(int64_t pts) { return pts * (media::TimelineRate::NsPerSecond / pts_rate_); }

  // Converts a pts in ns to |pts_rate_| units.
  int64_t from_ns(int64_t pts) { return pts * (pts_rate_ / media::TimelineRate::NsPerSecond); }

  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  bool renderer_responding_ = false;
  bool input_connection_ready_ = false;
  fit::closure when_input_connection_ready_;
  media::TimelineRate pts_rate_;
  int64_t last_supplied_pts_ns_ = Packet::kNoPts;
  int64_t last_departed_pts_ns_ = Packet::kNoPts;
  int64_t next_pts_to_assign_ = Packet::kNoPts;
  bool input_packet_request_outstanding_ = false;
  fit::closure prime_callback_;
  uint32_t bytes_per_frame_;
  bool flushed_ = true;
  int64_t min_lead_time_ns_;
  int64_t target_lead_time_ns_;
  async::TaskClosure demand_task_;

  size_t packet_bytes_outstanding_ = 0;
  size_t payload_buffer_size_ = 0;
  size_t expected_packet_size_ = 0;
  bool stall_logged_ = false;
  bool unsupported_rate_ = false;

  PacketTimingTracker arrivals_;
  PacketTimingTracker departures_;

  // Disallow copy, assign and move.
  FidlAudioRenderer(const FidlAudioRenderer&) = delete;
  FidlAudioRenderer(FidlAudioRenderer&&) = delete;
  FidlAudioRenderer& operator=(const FidlAudioRenderer&) = delete;
  FidlAudioRenderer& operator=(FidlAudioRenderer&&) = delete;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_AUDIO_RENDERER_H_
