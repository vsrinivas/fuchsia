// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/stream_sink_server.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

// static
std::shared_ptr<StreamSinkServer> StreamSinkServer::Create(
    std::shared_ptr<const FidlThread> thread,
    fidl::ServerEnd<fuchsia_media2::StreamSink> server_end, Args args) {
  return BaseFidlServer::Create(std::move(thread), std::move(server_end), std::move(args));
}

StreamSinkServer::StreamSinkServer(Args args)
    : format_(args.format),
      frac_frames_per_media_ticks_(
          TimelineRate::Product(format_.frac_frames_per_ns(), args.media_ticks_per_ns.Inverse())),
      payload_buffers_(std::move(args.payload_buffers)),
      command_queue_(std::make_shared<CommandQueue>()) {}

void StreamSinkServer::PutPacket(PutPacketRequestView request,
                                 PutPacketCompleter::Sync& completer) {
  TRACE_DURATION("audio", "StreamSink::PutPacket");
  ScopedThreadChecker checker(thread().checker());

  auto cleanup = fit::defer([this] {
    ScopedThreadChecker checker(thread().checker());
    ++fidl_calls_completed_;
  });

  // TODO(fxbug.dev/87651): For now, until the StreamSink API is finalized, we ignore errors in the
  // input and we ignore all unsupported cases.

  Fixed packet_start;
  switch (request->packet.timestamp.Which()) {
    case fuchsia_media2::wire::PacketTimestamp::Tag::kSpecified:
      // Media and frame timestamps share the same epoch. Hence, the translation is just a rate
      // change. See ../docs/timelines.md.
      packet_start =
          Fixed::FromRaw(frac_frames_per_media_ticks_.Scale(request->packet.timestamp.specified()));
      break;
    case fuchsia_media2::wire::PacketTimestamp::Tag::kUnspecifiedContinuous:
      packet_start = next_continuous_frame_;
      break;
    case fuchsia_media2::wire::PacketTimestamp::Tag::kUnspecifiedBestEffort:
      FX_LOGS(WARNING) << "Skipping packet: unspecified_best_effort timestamps not supported";
      return;
    default:
      FX_LOGS(WARNING) << "Skipping packet: unepxected packet timestamp tag = "
                       << static_cast<int>(request->packet.timestamp.Which());
      return;
  }

  if (request->packet.compression_properties.has_value()) {
    FX_LOGS(WARNING) << "Skipping packet: compression_properties not supported";
    return;
  }
  if (request->packet.encryption_properties) {
    FX_LOGS(WARNING) << "Skipping packet: encryption_properties not supported";
    return;
  }

  if (request->packet.payload.empty()) {
    FX_LOGS(WARNING) << "Skipping packet: payload ranges not specified";
    return;
  }
  if (request->packet.payload.count() > 1) {
    FX_LOGS(WARNING) << "Skipping packet: multiple payload ranges not supported";
    return;
  }

  auto& payload_range = request->packet.payload[0];
  auto buffer_it = payload_buffers_.find(payload_range.buffer_id);
  if (buffer_it == payload_buffers_.end()) {
    FX_LOGS(WARNING) << "Skipping packet: unknown payload buffer id " << payload_range.buffer_id;
    return;
  }

  // Since the offset is an unsigned integer, the payload is out-of-range if its endpoint is too
  // large or wraps around.
  const auto& buffer = *buffer_it->second;
  uint64_t payload_offset_end = payload_range.offset + payload_range.size;
  if (payload_offset_end > buffer.size() || payload_offset_end < payload_range.offset) {
    FX_LOGS(WARNING) << "Skipping packet: payload buffer out-of-range";
    return;
  }
  if (payload_range.size % format_.bytes_per_frame() != 0) {
    FX_LOGS(WARNING) << "Skipping packet: payload buffer has a non-integral number of frames";
    return;
  }

  PacketView packet({
      .format = format_,
      .start_frame = packet_start,
      .frame_count = static_cast<int64_t>(payload_range.size) / format_.bytes_per_frame(),
      .payload = static_cast<char*>(buffer.start()) + payload_range.offset,
  });

  next_continuous_frame_ = packet.end_frame();
  command_queue_->push(SimplePacketQueueProducerStage::PushPacketCommand{
      .packet = packet,
      .fence = std::move(request->release_fence),
  });
}

void StreamSinkServer::End(EndCompleter::Sync& completer) {
  // This is a no-op. We don't need to tell the mix threads when a stream has "ended".
  // It's sufficient to let the queue stay empty.
  ScopedThreadChecker checker(thread().checker());
  ++fidl_calls_completed_;
}

void StreamSinkServer::Clear(ClearRequestView request, ClearCompleter::Sync& completer) {
  TRACE_DURATION("audio", "StreamSink::Clear");
  ScopedThreadChecker checker(thread().checker());

  auto cleanup = fit::defer([this] {
    ScopedThreadChecker checker(thread().checker());
    ++fidl_calls_completed_;
  });

  command_queue_->push(SimplePacketQueueProducerStage::ClearCommand{
      .fence = std::move(request->completion_fence),
  });
}

}  // namespace media_audio
