// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_STREAM_SINK_CONSUMER_WRITER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_STREAM_SINK_CONSUMER_WRITER_H_

#include <fidl/fuchsia.media2/cpp/wire.h>
#include <lib/fidl/cpp/wire/client.h>

#include <memory>
#include <optional>

#include "src/media/audio/lib/format2/stream_converter.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/common/thread_safe_queue.h"
#include "src/media/audio/services/mixer/mix/consumer_stage.h"

namespace media_audio {

// Enables consumers to write to a FIDL StreamSink.
class StreamSinkConsumerWriter : public ConsumerStage::Writer {
 public:
  // Intermediate representation of a fuchsia.media2.Packet.
  class Packet {
   public:
    // Packets have the following properties:
    //
    // * `buffer` is the VMO which contains this packet.
    // * `payload_range` is the offset and size of the packet within `buffer`.
    // * `payload` is a pointer to the start of the packet within `buffer`.
    Packet(std::shared_ptr<MemoryMappedBuffer> buffer,
           fuchsia_media2::wire::PayloadRange payload_range, void* payload);

    // Recycles this packet for reuse. After recycling, the packet's write pointer is reset to the
    // start of the payload range, the timestamp is reset (as described below), and all further
    // calls to Append* and FramesRemaining will interpret source data using `stream_converter`.
    //
    // `timestamp` is the media timestamp of the first frame in the packet. This is either an
    // explicit int64_t value or the special value `std::nullopt`, which means "continuous with the
    // prior packet".
    void Recycle(std::shared_ptr<StreamConverter> stream_converter,
                 std::optional<int64_t> timestamp);

    // Appends data to this packet, advancing the packet's write pointer by up to `frame_count`.
    // Returns the number of frames appended, or zero if full.
    //
    // REQUIRED: Must call Recycle at least once before appending any data.
    int64_t AppendData(int64_t frame_count, const void* data);
    int64_t AppendSilence(int64_t frame_count);

    // Reports the number of frames that can be appended to this packet.
    //
    // REQUIRED: Must call Recycle at least once before calling this method.
    int64_t FramesRemaining() const;

    // Conversion to a FIDL object that can be passed to PutPacket.
    fuchsia_media2::wire::Packet ToFidl(fidl::AnyArena& arena) const;

   private:
    int64_t bytes_per_frame() const { return stream_converter_->dest_format().bytes_per_frame(); }

    // We hold `buffer_` to ensure that the underlying VMO is not unmapped before this packet is
    // discarded. This avoids accidental memory errors in `Append` methods.
    const std::shared_ptr<MemoryMappedBuffer> buffer_;
    const uint32_t payload_range_buffer_id_;
    const uint64_t payload_range_offset_;
    const uint64_t payload_range_max_size_;

    // These are reset by Recycle.
    uint64_t write_offset_ = 0;  // relative to buffer_->offset(payload_range_offset_)
    std::shared_ptr<StreamConverter> stream_converter_;
    std::optional<int64_t> timestamp_;
  };

  // Packets are transferred as unique_ptrs because the Packet class is not safe for concurrent use.
  // unique_ptr guarantees that at most one thread can reference a Packet at any time, which avoids
  // data races.
  using PacketQueue = ThreadSafeQueue<std::unique_ptr<Packet>>;

  struct Args {
    // Format of packets sent to this StreamSink.
    Format format;

    // Ticks of media time per nanoseconds of reference time.
    TimelineRate media_ticks_per_ns;

    // Callback which invokes fuchsia.media2.StreamSink/PutPacket.
    std::function<void(std::unique_ptr<Packet>)> call_put_packet;

    // Callback which invokes fuchsia.media2.StreamSink/End.
    std::function<void()> call_end;

    // TODO(fxbug.dev/114393): Callback to report overflow.

    // Queue of objects to use for future packets. In the steady state, objects are pulled from this
    // queue, written to, forwarded to `call_put_packet`, then released back into this queue to be
    // recycled for another packet.
    //
    // When this empty, the StreamSink channel is full. The other side of this queue must recycle
    // packets quickly enough to avoid overflow. Put differently, if the other side of the
    // StreamSink processes data too slowly, the StreamSink channel will overflow. When overflow
    // occurs, writes are dropped.
    std::shared_ptr<PacketQueue> recycled_packet_queue;
  };

  explicit StreamSinkConsumerWriter(Args args);

  // Implements ConsumerStage::Writer.
  void WriteData(int64_t start_frame, int64_t length, const void* payload) final;
  void WriteSilence(int64_t start_frame, int64_t length) final;
  void End() final;

 private:
  void WriteInternal(int64_t start_frame, int64_t length, const void* payload);
  void SendCurrentPacket();

  const std::shared_ptr<StreamConverter> stream_converter_;
  const TimelineRate media_ticks_per_frame_;
  const std::function<void(std::unique_ptr<Packet>)> call_put_packet_;
  const std::function<void()> call_end_;
  const std::shared_ptr<PacketQueue> recycled_packet_queue_;

  std::unique_ptr<Packet> current_packet_;
  std::optional<int64_t> next_continuous_frame_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_STREAM_SINK_CONSUMER_WRITER_H_
