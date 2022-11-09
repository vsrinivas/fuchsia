// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_SERVER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_SERVER_H_

#include <fidl/fuchsia.audio/cpp/wire.h>
#include <fidl/fuchsia.media2/cpp/wire.h>
#include <zircon/errors.h>

#include <memory>
#include <unordered_map>

#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"

namespace media_audio {

class StreamSinkServer
    : public BaseFidlServer<StreamSinkServer, fidl::WireServer, fuchsia_audio::StreamSink> {
 public:
  using CommandQueue = SimplePacketQueueProducerStage::CommandQueue;

  struct Args {
    // Format of packets sent to this StreamSink.
    Format format;

    // Ticks of media time per nanoseconds of reference time.
    TimelineRate media_ticks_per_ns;

    // Payload buffers available to this StreamSink, indexed by buffer ID.
    std::unordered_map<uint32_t, std::shared_ptr<MemoryMappedBuffer>> payload_buffers;

    // Initial segment ID.
    int64_t initial_segment_id;
  };

  // The returned server will live until the `server_end` channel is closed.
  static std::shared_ptr<StreamSinkServer> Create(
      std::shared_ptr<const FidlThread> thread,
      fidl::ServerEnd<fuchsia_audio::StreamSink> server_end, Args args);

  // Returns the format of packets received by this StreamSink.
  const Format& format() const { return format_; }

  // Returns the queue used to communicate with the producer.
  std::shared_ptr<CommandQueue> command_queue() const { return command_queue_; }

  // Implementation of fidl::WireServer<fuchsia_audio::StreamSink>.
  void PutPacket(PutPacketRequestView request, PutPacketCompleter::Sync& completer) final;
  void StartSegment(StartSegmentRequestView request, StartSegmentCompleter::Sync& completer) final;
  void End(EndCompleter::Sync& completer) final;
  void WillClose(WillCloseRequestView request, WillCloseCompleter::Sync& completer) final;

  // Clear all packets up through the given segment ID.
  void ReleasePackets(int64_t before_segment_id);

 private:
  static inline constexpr std::string_view kClassName = "StreamSinkServer";
  template <typename ServerT, template <typename T> typename FidlServerT, typename ProtocolT>
  friend class BaseFidlServer;

  explicit StreamSinkServer(Args args);
  void CloseWithReason(fuchsia_media2::ConsumerClosedReason reason);

  const Format format_;
  const TimelineRate frac_frames_per_media_ticks_;
  const std::unordered_map<uint32_t, std::shared_ptr<MemoryMappedBuffer>> payload_buffers_;
  const std::shared_ptr<CommandQueue> command_queue_;

  // The frame timestamp for the first frame in the next continuous packet.
  // Defaults to 0 for the first packet.
  TA_GUARDED(thread().checker()) Fixed next_continuous_frame_{0};

  // Current segment ID.
  TA_GUARDED(thread().checker()) int64_t segment_id_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_SERVER_H_
