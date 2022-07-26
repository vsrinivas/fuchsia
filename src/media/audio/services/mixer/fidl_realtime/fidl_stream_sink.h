// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_FIDL_STREAM_SINK_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_FIDL_STREAM_SINK_H_

#include <fidl/fuchsia.media2/cpp/wire.h>
#include <zircon/errors.h>

#include <memory>
#include <unordered_map>

#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/packet_queue_producer_stage.h"

namespace media_audio {

class FidlStreamSink : public BaseFidlServer<FidlStreamSink, fuchsia_media2::StreamSink> {
 public:
  using CommandQueue = PacketQueueProducerStage::CommandQueue;

  struct Args {
    // Format of packets sent to this StreamSink.
    Format format;

    // Ticks of media time per nanoseconds of reference time.
    TimelineRate media_ticks_per_ns;

    // Payload buffers available to this StreamSink, indexed by buffer ID.
    std::unordered_map<uint32_t, std::shared_ptr<MemoryMappedBuffer>> payload_buffers;
  };

  // The returned server will live until the `server_end` channel is closed.
  static std::shared_ptr<FidlStreamSink> Create(
      std::shared_ptr<const FidlThread> thread,
      fidl::ServerEnd<fuchsia_media2::StreamSink> server_end, Args args);

  // Adds an outgoing producer queue. Incoming FIDL commands are forwarded to each queue.
  void AddProducerQueue(std::shared_ptr<CommandQueue> q) TA_REQ(thread().checker());

  // Removes an outgoing producer queue that was previously added. After this returns, `q`
  // will no longer receive forwarded FIDL commands.
  void RemoveProducerQueue(std::shared_ptr<CommandQueue> q) TA_REQ(thread().checker());

  // Implementation of fidl::WireServer<fuchsia_media2::StreamSink>.
  void PutPacket(PutPacketRequestView request, PutPacketCompleter::Sync& completer) override;
  void End(EndRequestView request, EndCompleter::Sync& completer) override;
  void Clear(ClearRequestView request, ClearCompleter::Sync& completer) override;

 private:
  static inline constexpr std::string_view Name = "FidlStreamSink";
  template <class ServerT, class ProtocolT>
  friend class BaseFidlServer;

  explicit FidlStreamSink(Args args);

  // For each queue, call `fn` and pass a duplicate of `fence`.
  void ForEachQueueWithDuplicateFence(zx::eventpair fence,
                                      std::function<void(CommandQueue&, zx::eventpair)> fn)
      TA_REQ(thread().checker());

  const Format format_;
  const TimelineRate frac_frames_per_media_ticks_;
  const std::unordered_map<uint32_t, std::shared_ptr<MemoryMappedBuffer>> payload_buffers_;

  std::unordered_map<CommandQueue*, std::shared_ptr<CommandQueue>> queues_
      TA_GUARDED(thread().checker());

  // The frame timestamp for the first frame in the next continuous packet.
  // Defaults to 0 for the first packet.
  Fixed next_continuous_frame_{0};

  // If set, called after each FIDL method call completes. This is exclusively for tests: since
  // StreamSink uses one-way protocols, tests cannot wait for FIDL call completion without a
  // backdoor like this.
  friend class FidlStreamSinkTest;
  std::function<void()> on_method_complete_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_FIDL_STREAM_SINK_H_
