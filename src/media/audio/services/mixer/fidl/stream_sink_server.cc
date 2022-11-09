// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/stream_sink_server.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

using fuchsia_audio::wire::Timestamp;
using fuchsia_media2::ConsumerClosedReason;

// static
std::shared_ptr<StreamSinkServer> StreamSinkServer::Create(
    std::shared_ptr<const FidlThread> thread, fidl::ServerEnd<fuchsia_audio::StreamSink> server_end,
    Args args) {
  return BaseFidlServer::Create(std::move(thread), std::move(server_end), std::move(args));
}

StreamSinkServer::StreamSinkServer(Args args)
    : format_(args.format),
      frac_frames_per_media_ticks_(
          TimelineRate::Product(format_.frac_frames_per_ns(), args.media_ticks_per_ns.Inverse())),
      payload_buffers_(std::move(args.payload_buffers)),
      command_queue_(std::make_shared<CommandQueue>()),
      segment_id_(args.initial_segment_id) {}

void StreamSinkServer::PutPacket(PutPacketRequestView request,
                                 PutPacketCompleter::Sync& completer) {
  TRACE_DURATION("audio", "StreamSink::PutPacket");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_packet()) {
    FX_LOGS(WARNING) << "PutPacket: missing packet";
    CloseWithReason(ConsumerClosedReason::kInvalidPacket);
    return;
  }
  if (!request->packet().has_payload()) {
    FX_LOGS(WARNING) << "PutPacket: missing payload";
    CloseWithReason(ConsumerClosedReason::kInvalidPacket);
    return;
  }

  const auto& packet = request->packet();
  if (packet.has_flags() || packet.has_front_frames_to_drop() || packet.has_back_frames_to_drop() ||
      packet.has_encryption_properties()) {
    FX_LOGS(WARNING) << "PutPacket: unsupported field";
    CloseWithReason(ConsumerClosedReason::kInvalidPacket);
    return;
  }

  const auto which_timestamp =
      packet.has_timestamp() ? packet.timestamp().Which() : Timestamp::Tag::kUnspecifiedBestEffort;

  Fixed packet_start;
  switch (which_timestamp) {
    case Timestamp::Tag::kSpecified:
      // Media and frame timestamps share the same epoch. Hence, the translation is just a rate
      // change. See ../docs/timelines.md.
      packet_start =
          Fixed::FromRaw(frac_frames_per_media_ticks_.Scale(packet.timestamp().specified()));
      break;
    case Timestamp::Tag::kUnspecifiedContinuous:
      packet_start = next_continuous_frame_;
      break;
    case Timestamp::Tag::kUnspecifiedBestEffort:
      // TODO(fxbug.dev/114712): support unspecified_best_effort
      FX_LOGS(WARNING) << "Skipping packet: unspecified_best_effort timestamps not supported";
      return;
    default:
      FX_LOGS(WARNING) << "PutPacket: unepxected packet timestamp tag = "
                       << static_cast<int>(packet.timestamp().Which());
      CloseWithReason(ConsumerClosedReason::kInvalidPacket);
      return;
  }

  const auto& payload = packet.payload();
  auto buffer_it = payload_buffers_.find(payload.buffer_id);
  if (buffer_it == payload_buffers_.end()) {
    FX_LOGS(WARNING) << "PutPacket: unknown payload buffer id " << payload.buffer_id;
    CloseWithReason(ConsumerClosedReason::kInvalidPacket);
    return;
  }

  // Since the offset is an unsigned integer, the payload is out-of-range if its endpoint is too
  // large or wraps around.
  const auto& buffer = *buffer_it->second;
  const uint64_t payload_offset_end = payload.offset + payload.size;
  if (payload_offset_end > buffer.size() || payload_offset_end < payload.offset) {
    FX_LOGS(WARNING) << "PutPacket: payload buffer out-of-range: offset=" << payload.offset
                     << ", size=" << payload.size << " buffer_size=" << buffer.size();
    CloseWithReason(ConsumerClosedReason::kInvalidPacket);
    return;
  }
  if (payload.size % format_.bytes_per_frame() != 0) {
    FX_LOGS(WARNING) << "PutPacket: payload buffer has a non-integral number of frames";
    CloseWithReason(ConsumerClosedReason::kInvalidPacket);
    return;
  }

  PacketView packet_view({
      .format = format_,
      .start_frame = packet_start,
      .frame_count = static_cast<int64_t>(payload.size) / format_.bytes_per_frame(),
      .payload = static_cast<char*>(buffer.start()) + payload.offset,
  });

  next_continuous_frame_ = packet_view.end_frame();
  command_queue_->push(SimplePacketQueueProducerStage::PushPacketCommand{
      .packet = packet_view,
      .fence = request->has_release_fence() ? std::move(request->release_fence()) : zx::eventpair(),
  });
}

void StreamSinkServer::StartSegment(StartSegmentRequestView request,
                                    StartSegmentCompleter::Sync& completer) {
  TRACE_DURATION("audio", "StreamSink::StartSegment");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_segment_id() || request->segment_id() <= segment_id_) {
    FX_LOGS(WARNING) << "StartSegment: invalid segment_id";
    CloseWithReason(ConsumerClosedReason::kProtocolError);
    return;
  }

  segment_id_ = request->segment_id();
}

void StreamSinkServer::End(EndCompleter::Sync& completer) {
  // This is a no-op. We don't need to tell the mixer when a stream has "ended".
  // It's sufficient to let the queue stay empty.
}

void StreamSinkServer::WillClose(WillCloseRequestView request,
                                 WillCloseCompleter::Sync& completer) {
  TRACE_DURATION("audio", "StreamSink::WillClose");
  ScopedThreadChecker checker(thread().checker());

  if (request->has_reason()) {
    FX_LOGS(INFO) << "StreamSink closing with reason " << static_cast<uint32_t>(request->reason());
  }
}

void StreamSinkServer::ReleasePackets(int64_t before_segment_id) {
  TRACE_DURATION("audio", "StreamSink::ReleasePackets");
  ScopedThreadChecker checker(thread().checker());

  command_queue_->push(SimplePacketQueueProducerStage::ReleasePacketsCommand{
      .before_segment_id = before_segment_id,
  });
}

void StreamSinkServer::CloseWithReason(ConsumerClosedReason reason) {
  fidl::Arena<> arena;
  std::ignore = fidl::WireSendEvent(binding())->OnWillClose(
      fuchsia_audio::wire::StreamSinkOnWillCloseRequest::Builder(arena).reason(reason).Build());
  Shutdown();
}

}  // namespace media_audio
