// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_SIMPLE_STREAM_SINK_IMPL_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_SIMPLE_STREAM_SINK_IMPL_H_

#include <fuchsia/media/playback/cpp/fidl.h>

#include <unordered_map>

#include "lib/fidl/cpp/binding.h"
#include "src/lib/fxl/synchronization/thread_checker.h"
#include "src/media/playback/mediaplayer/graph/nodes/node.h"

namespace media_player {

// Simple stream sink is composed of a StreamSink and a StreamBufferSet
class SimpleStreamSinkImpl : public Node, public fuchsia::media::SimpleStreamSink {
 public:
  // Creates a simple stream sink.
  static std::shared_ptr<SimpleStreamSinkImpl> Create(
      const StreamType& output_stream_type, media::TimelineRate pts_rate,
      fidl::InterfaceRequest<fuchsia::media::SimpleStreamSink> request,
      fit::closure connection_failure_callback);

  // Creates a simple stream sink from StreamSink. Buffers must be managed via
  // some other method
  static std::shared_ptr<SimpleStreamSinkImpl> Create(
      const StreamType& output_stream_type, media::TimelineRate pts_rate,
      fidl::InterfaceRequest<fuchsia::media::StreamSink> stream_sink_request,
      fit::closure connection_failure_callback);

  SimpleStreamSinkImpl(const StreamType& output_stream_type, media::TimelineRate pts_rate,
                       fidl::InterfaceRequest<fuchsia::media::SimpleStreamSink> request,
                       fit::closure connection_failure_callback);

  ~SimpleStreamSinkImpl() override;

  const StreamType& output_stream_type() const {
    FX_DCHECK(output_stream_type_);
    return *output_stream_type_;
  }

  // Node implementation.
  const char* label() const override { return "simple stream sink"; }

  void Dump(std::ostream& os) const override;

  void ConfigureConnectors() override;

  void FlushOutput(size_t output_index, fit::closure callback) override;

  void RequestOutputPacket() override;

  // SimpleStreamSink implementation:
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) override;

  void RemovePayloadBuffer(uint32_t id) override;

  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) override;

  void SendPacketNoReply(fuchsia::media::StreamPacket packet) override;

  void EndOfStream() override;

  void DiscardAllPackets(DiscardAllPacketsCallback callback) override;

  void DiscardAllPacketsNoReply() override;

 private:
  struct PayloadVmoInfo {
    fbl::RefPtr<PayloadVmo> vmo_;
    uint32_t packet_count_{};
  };

  FXL_DECLARE_THREAD_CHECKER(thread_checker_);

  std::unique_ptr<StreamType> output_stream_type_;
  media::TimelineRate pts_rate_;
  fidl::Binding<fuchsia::media::SimpleStreamSink> binding_;
  fit::closure connection_failure_callback_;
  int64_t pts_ = 0;
  std::unordered_map<uint32_t, PayloadVmoInfo> payload_vmo_infos_by_id_;
  bool flushing_ = false;

  // Disallow copy and assign.
  SimpleStreamSinkImpl(const SimpleStreamSinkImpl&) = delete;
  SimpleStreamSinkImpl& operator=(const SimpleStreamSinkImpl&) = delete;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_SIMPLE_STREAM_SINK_IMPL_H_
