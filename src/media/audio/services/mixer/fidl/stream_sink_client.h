// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_CLIENT_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_CLIENT_H_

#include <fidl/fuchsia.media2/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/zx/eventpair.h>

#include <map>
#include <memory>

#include "src/media/audio/services/common/fidl_thread.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/stream_sink_consumer_writer.h"

namespace media_audio {

// This object manages a FIDL StreamSink client connection.
class StreamSinkClient : public std::enable_shared_from_this<StreamSinkClient> {
 public:
  using Packet = StreamSinkConsumerWriter::Packet;
  using PacketQueue = StreamSinkConsumerWriter::PacketQueue;

  struct Args {
    // Format of packets sent to this StreamSink.
    Format format;

    // Size of each packet.
    int64_t frames_per_packet;

    // FIDL handle.
    fidl::WireSharedClient<fuchsia_media2::StreamSink> client;

    // Payload buffers available to this StreamSink, indexed by buffer ID.
    // Each buffer must be large enough to fit at least one packet.
    std::map<uint32_t, std::shared_ptr<MemoryMappedBuffer>> payload_buffers;

    // Queue to forward new and recycled packets.
    std::shared_ptr<PacketQueue> recycled_packet_queue;

    // Thread on which this client runs.
    std::shared_ptr<const FidlThread> thread;
  };

  explicit StreamSinkClient(Args args);

  // Calls fuchsia_media2.StreamSink/PutPacket.
  // This method is safe to call from any thread.
  void PutPacket(std::unique_ptr<Packet> packet);

  // Calls fuchsia_media2.StreamSink/End.
  // This method is safe to call from any thread.
  void End();

  // Returns the thread used by this client.
  // This method is safe to call from any thread.
  const FidlThread& thread() const { return *thread_; }

  // Shuts down this client. This FIDL connection will be closed. All future FIDL calls will be
  // dropped. In-flight packets will not be recycled.
  void Shutdown() TA_REQ(thread().checker());

 private:
  const std::map<uint32_t, std::shared_ptr<MemoryMappedBuffer>> payload_buffers_;
  const std::shared_ptr<PacketQueue> recycled_packet_queue_;
  const std::shared_ptr<const FidlThread> thread_;

  std::optional<fidl::WireSharedClient<fuchsia_media2::StreamSink>> client_;

  struct InflightPacket {
    InflightPacket(std::shared_ptr<StreamSinkClient> c, std::unique_ptr<Packet> p, zx::eventpair f)
        : client(std::move(c)),
          packet(std::move(p)),
          fence(std::move(f)),
          wait(fence.get(), ZX_EVENTPAIR_PEER_CLOSED) {}

    std::shared_ptr<StreamSinkClient> client;
    std::unique_ptr<Packet> packet;
    zx::eventpair fence;
    // Must be last so this is destructed before the `fence` handle is dropped.
    async::WaitOnce wait;
  };

  TA_GUARDED(thread().checker())
  std::unordered_map<InflightPacket*, std::unique_ptr<InflightPacket>> inflight_packets_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_CLIENT_H_
