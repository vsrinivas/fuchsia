// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/flog_viewer/accumulator.h"
#include "garnet/bin/flog_viewer/channel_handler.h"
#include "garnet/bin/flog_viewer/counted.h"
#include "garnet/bin/flog_viewer/handlers/media_packet_consumer.h"
#include "garnet/bin/flog_viewer/handlers/media_timeline_control_point.h"
#include "garnet/bin/flog_viewer/tracked.h"
#include "lib/media/fidl/logs/media_renderer_channel.fidl.h"
#include "lib/media/timeline/timeline_rate.h"

namespace flog {
namespace handlers {

class MediaRendererAccumulator;

// Handler for MediaRendererChannel messages.
class MediaRenderer : public ChannelHandler,
                      public media::logs::MediaRendererChannel {
 public:
  MediaRenderer(const std::string& format);

  ~MediaRenderer() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaRendererChannel implementation.
  void BoundAs(uint64_t koid) override;

  void Config(fidl::Array<media::MediaTypeSetPtr> supported_types,
              uint64_t consumer_address,
              uint64_t timeline_control_point_address) override;

  void SetMediaType(media::MediaTypePtr type) override;

  void PtsRate(uint32_t ticks, uint32_t seconds) override;

  void EngagePacket(int64_t current_pts,
                    int64_t packet_pts,
                    uint64_t packet_label) override;

  void RenderRange(int64_t pts, uint32_t duration) override;

 private:
  MediaTimelineControlPoint* GetTimelineControlPoint();

  MediaPacketConsumer* GetConsumer();

  void RecordPacketEarliness(MediaPacketConsumerAccumulator::Packet* packet,
                             MediaTimelineControlPoint* timeline_control_point);

  media::logs::MediaRendererChannelStub stub_;
  std::shared_ptr<MediaRendererAccumulator> accumulator_;
  media::TimelineRate audio_frame_rate_;
  uint32_t audio_frame_size_;
  int64_t expected_range_pts_ = media::MediaPacket::kNoTimestamp;
  uint64_t earliness_prev_packet_label_ = 0;
  bool end_of_stream_ = false;
  bool was_paused_ = true;
};

// Status of a media renderer as understood by MediaRenderer.
class MediaRendererAccumulator : public Accumulator {
 public:
  MediaRendererAccumulator();
  ~MediaRendererAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  fidl::Array<media::MediaTypeSetPtr> supported_types_;
  std::shared_ptr<Channel> consumer_channel_;
  std::shared_ptr<Channel> timeline_control_point_channel_;
  media::MediaTypePtr type_;
  media::TimelineRate pts_rate_ = media::TimelineRate::NsPerSecond;
  Counted preroll_packets_;
  Counted preroll_renders_;
  Tracked packet_earliness_ns_;
  Counted starved_no_packet_;
  Tracked starved_ns_;
  Counted missing_packets_;
  Tracked gaps_in_frames_before_first_;
  Tracked gaps_in_frames_no_packet_;
  Tracked gaps_in_frames_between_packets_;
  Tracked gaps_in_frames_end_of_stream_;

  friend class MediaRenderer;
};

}  // namespace handlers
}  // namespace flog
