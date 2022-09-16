// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/graph_server.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/vmo.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/fidl/meta_producer_node.h"
#include "src/media/audio/services/mixer/fidl_realtime/stream_sink_server.h"

namespace media_audio {

namespace {

template <typename T>
std::string_view NameOrEmpty(const T& object) {
  if (object.has_name()) {
    return object.name().get();
  }
  return "";
}

std::string ClockNameFromNodeName(std::string_view node_name) {
  return std::string(node_name) + "Clock";
}

// Validates that `ring_buffer` has a compatible VMO and format. Returns the mapped buffer and
// format on success, else `std::nullopt`. Caller must check that fields are not missing.
std::optional<std::pair<std::shared_ptr<MemoryMappedBuffer>, Format>> ValidateRingBuffer(
    std::string_view debug_description, const fuchsia_audio::wire::RingBuffer& ring_buffer,
    const bool writable) {
  auto format_result = Format::Create(ring_buffer.format());
  if (!format_result.is_ok()) {
    FX_LOGS(WARNING) << debug_description
                     << ": invalid ring buffer format: " << format_result.error();
    return std::nullopt;
  }

  auto mapped_buffer_result = MemoryMappedBuffer::Create(ring_buffer.vmo(), writable);
  if (!mapped_buffer_result.is_ok()) {
    FX_LOGS(WARNING) << debug_description
                     << ": invalid ring buffer vmo: " << mapped_buffer_result.error();
    return std::nullopt;
  }

  auto format = format_result.value();
  auto mapped_buffer = mapped_buffer_result.value();

  if (ring_buffer.producer_bytes() % format.bytes_per_frame() != 0 ||
      ring_buffer.consumer_bytes() % format.bytes_per_frame() != 0 ||
      ring_buffer.producer_bytes() + ring_buffer.consumer_bytes() > mapped_buffer->content_size()) {
    FX_LOGS(WARNING) << debug_description << ": invalid ring buffer partition:"
                     << " producer_bytes=" << ring_buffer.producer_bytes()
                     << ", consumer_bytes=" << ring_buffer.consumer_bytes()
                     << ", content_size=" << mapped_buffer->content_size()
                     << ", bytes_per_frame=" << format.bytes_per_frame();
    return std::nullopt;
  }

  if (format.bytes_per_frame() > static_cast<int64_t>(mapped_buffer->content_size())) {
    FX_LOGS(WARNING) << debug_description << ": ring buffer too small for format, content_size="
                     << mapped_buffer->content_size();
    return std::nullopt;
  }

  return std::make_pair(std::move(mapped_buffer), format);
}

}  // namespace

// static
std::shared_ptr<GraphServer> GraphServer::Create(
    std::shared_ptr<const FidlThread> main_fidl_thread,
    fidl::ServerEnd<fuchsia_audio_mixer::Graph> server_end, Args args) {
  return BaseFidlServer::Create(std::move(main_fidl_thread), std::move(server_end),
                                std::move(args));
}

GraphServer::GraphServer(Args args)
    : name_(std::move(args.name)),
      realtime_fidl_thread_(std::move(args.realtime_fidl_thread)),
      clock_registry_(std::move(args.clock_registry)) {}

void GraphServer::CreateProducer(CreateProducerRequestView request,
                                 CreateProducerCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateProducer");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_direction() || !request->has_data_source()) {
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
    return;
  }

  const auto name = NameOrEmpty(*request);
  std::optional<MetaProducerNode::DataSource> source;
  std::optional<Format> format;
  std::optional<zx_koid_t> reference_clock_koid;

  if (request->data_source().is_stream_sink()) {
    auto& stream_sink = request->data_source().stream_sink();
    if (!stream_sink.has_server_end() || !stream_sink.server_end().is_valid() ||
        !stream_sink.has_format() ||  //
        !stream_sink.has_reference_clock() || !stream_sink.reference_clock().has_handle() ||
        !stream_sink.has_payload_buffer() || !stream_sink.payload_buffer().is_valid() ||
        !stream_sink.has_media_ticks_per_second_numerator() ||
        !stream_sink.has_media_ticks_per_second_denominator()) {
      FX_LOGS(WARNING) << "CreateProducer(StreamSink): missing field";
      completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
      return;
    }

    auto format_result = Format::Create(stream_sink.format());
    if (!format_result.is_ok()) {
      FX_LOGS(WARNING) << "CreateProducer(StreamSink): invalid stream sink format: "
                       << format_result.error();
      completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
      return;
    }

    auto payload_buffer_result =
        MemoryMappedBuffer::Create(stream_sink.payload_buffer(), /*writable=*/false);
    if (!payload_buffer_result.is_ok()) {
      FX_LOGS(WARNING) << "CreateProducer(StreamSink): invalid stream sink payload buffer: "
                       << payload_buffer_result.error();
      completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
      return;
    }

    if (stream_sink.media_ticks_per_second_numerator() == 0 ||
        stream_sink.media_ticks_per_second_denominator() == 0) {
      FX_LOGS(WARNING) << "CreateProducer(StreamSink): invalid stream sink media ticks/second="
                       << stream_sink.media_ticks_per_second_numerator() << "/"
                       << stream_sink.media_ticks_per_second_denominator();
      completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
      return;
    }

    auto clock = LookupClock(stream_sink.reference_clock(), name);
    if (!clock) {
      FX_LOGS(WARNING) << "CreateProducer(StreamSink): invalid clock";
      completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
      return;
    }

    reference_clock_koid = clock->koid();
    format = format_result.value();
    source = StreamSinkServer::Create(realtime_fidl_thread_, std::move(stream_sink.server_end()),
                                      StreamSinkServer::Args{
                                          .format = *format,
                                          .media_ticks_per_ns = TimelineRate(
                                              stream_sink.has_media_ticks_per_second_numerator(),
                                              stream_sink.has_media_ticks_per_second_denominator()),
                                          .payload_buffers = {{0, payload_buffer_result.value()}},
                                      });

  } else if (request->data_source().is_ring_buffer()) {
    auto& ring_buffer = request->data_source().ring_buffer();
    if (!ring_buffer.has_vmo() || !ring_buffer.vmo().is_valid() ||  //
        !ring_buffer.has_format() ||                                //
        !ring_buffer.has_producer_bytes() ||                        //
        !ring_buffer.has_consumer_bytes() ||                        //
        !ring_buffer.has_reference_clock() || !ring_buffer.reference_clock().is_valid()) {
      FX_LOGS(WARNING) << "CreateProducer(RingBuffer): missing field";
      completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
      return;
    }

    auto result = ValidateRingBuffer("CreateProducer(RingBuffer)", ring_buffer, false);
    if (!result) {
      completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
      return;
    }

    auto clock = LookupClock(ring_buffer, name);
    if (!clock) {
      FX_LOGS(WARNING) << "CreateProducer(StreamSink): invalid clock";
      completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
      return;
    }

    reference_clock_koid = clock->koid();
    format = result->second;
    source = RingBuffer::Create({
        .format = *format,
        .reference_clock_koid = *reference_clock_koid,
        .buffer = std::move(result->first),
        .producer_frames =
            static_cast<int64_t>(ring_buffer.producer_bytes()) / format->bytes_per_frame(),
        .consumer_frames =
            static_cast<int64_t>(ring_buffer.consumer_bytes()) / format->bytes_per_frame(),
    });

  } else {
    FX_LOGS(WARNING) << "Unsupported ProducerDataSource: "
                     << static_cast<int>(request->data_source().Which());
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kUnsupportedOption);
    return;
  }

  const auto id = NextNodeId();
  nodes_[id] = MetaProducerNode::Create({
      .name = name,
      .pipeline_direction = request->direction(),
      .format = *format,
      .reference_clock_koid = *reference_clock_koid,
      .data_source = std::move(*source),
      .detached_thread = detached_thread_,
  });

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphCreateProducerResponse::Builder(arena).id(id).Build());
}

void GraphServer::CreateConsumer(CreateConsumerRequestView request,
                                 CreateConsumerCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateConsumer");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::CreateMixer(CreateMixerRequestView request,
                              CreateMixerCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateMixer");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::CreateSplitter(CreateSplitterRequestView request,
                                 CreateSplitterCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateSplitter");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::CreateCustom(CreateCustomRequestView request,
                               CreateCustomCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateCustom");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::DeleteNode(DeleteNodeRequestView request, DeleteNodeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteNode");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::CreateEdge(CreateEdgeRequestView request, CreateEdgeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateEdge");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::DeleteEdge(DeleteEdgeRequestView request, DeleteEdgeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteEdge");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::CreateThread(CreateThreadRequestView request,
                               CreateThreadCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateThread");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::DeleteThread(DeleteThreadRequestView request,
                               DeleteThreadCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteThread");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::CreateGainControl(CreateGainControlRequestView request,
                                    CreateGainControlCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateGainControl");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::DeleteGainControl(DeleteGainControlRequestView request,
                                    DeleteGainControlCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteGainControl");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

void GraphServer::CreateGraphControlledReferenceClock(
    CreateGraphControlledReferenceClockCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateGraphControlledReferenceClock");
  ScopedThreadChecker checker(thread().checker());

  auto result = clock_registry_->CreateGraphControlledClock();
  if (!result.is_ok()) {
    completer.ReplyError(result.status_value());
    return;
  }

  auto handle = std::move(result.value().second);
  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphCreateGraphControlledReferenceClockResponse::Builder(arena)
          .reference_clock(std::move(handle))
          .Build());
}

void GraphServer::ForgetGraphControlledReferenceClock(
    ForgetGraphControlledReferenceClockRequestView request,
    ForgetGraphControlledReferenceClockCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::ForgetGraphControlledReferenceClock");
  ScopedThreadChecker checker(thread().checker());
  FX_CHECK(false) << "not implemented";
}

NodeId GraphServer::NextNodeId() {
  auto id = next_node_id_++;
  FX_CHECK(id != kInvalidId);
  return id;
}

std::shared_ptr<Clock> GraphServer::LookupClock(fuchsia_audio_mixer::wire::ReferenceClock& clock,
                                                std::string_view node_name) {
  if (!clock.has_handle()) {
    return nullptr;
  }
  std::string name =
      clock.has_name() ? std::string(clock.name().get()) : ClockNameFromNodeName(node_name);
  auto domain = clock.has_domain() ? clock.domain() : Clock::kExternalDomain;
  return LookupClock(std::move(clock.handle()), domain, name);
}

std::shared_ptr<Clock> GraphServer::LookupClock(fuchsia_audio::wire::RingBuffer& ring_buffer,
                                                std::string_view node_name) {
  if (!ring_buffer.has_reference_clock()) {
    return nullptr;
  }
  return LookupClock(std::move(ring_buffer.reference_clock()),
                     ring_buffer.has_reference_clock_domain() ? ring_buffer.reference_clock_domain()
                                                              : Clock::kExternalDomain,
                     ClockNameFromNodeName(node_name));
}

std::shared_ptr<Clock> GraphServer::LookupClock(zx::clock handle, uint32_t domain,
                                                std::string_view name) {
  if (!handle.is_valid()) {
    return nullptr;
  }
  if (auto result = clock_registry_->FindClock(handle); result.is_ok()) {
    return result.value();
  }
  if (auto result = clock_registry_->CreateUserControlledClock(std::move(handle), name, domain);
      result.is_ok()) {
    return result.value();
  }
  return nullptr;
}

}  // namespace media_audio
