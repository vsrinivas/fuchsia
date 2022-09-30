// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/graph_server.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/vmo.h>

#include <type_traits>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/fidl/consumer_node.h"
#include "src/media/audio/services/mixer/fidl/custom_node.h"
#include "src/media/audio/services/mixer/fidl/meta_producer_node.h"
#include "src/media/audio/services/mixer/fidl_realtime/stream_sink_client.h"
#include "src/media/audio/services/mixer/fidl_realtime/stream_sink_server.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"
#include "src/media/audio/services/mixer/mix/ring_buffer_consumer_writer.h"
#include "src/media/audio/services/mixer/mix/stream_sink_consumer_writer.h"

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

// Looks up a clock by `zx::clock` handle. If none exists in `registry`, creates an unadjustable
// wrapper clock with `factory` and adds that clock to `registry`.
zx::status<std::shared_ptr<Clock>> LookupClock(ClockRegistry& registry, ClockFactory& factory,
                                               zx::clock handle, uint32_t domain,
                                               std::string_view name) {
  if (auto result = registry.Find(handle); result.is_ok()) {
    return zx::ok(result.value());
  }
  auto clock_result =
      factory.CreateWrappedClock(std::move(handle), name, domain, /*adjustable=*/false);
  if (!clock_result.is_ok()) {
    return clock_result.take_error();
  }

  registry.Add(clock_result.value());
  return zx::ok(std::move(clock_result.value()));
}

// Validates `stream_sink` and translates from FIDL types to internal C++ types. This is intended to
// be used with StreamSinkProducer and StreamSinkConsumer types.
struct StreamSinkInfo {
  std::shared_ptr<MemoryMappedBuffer> payload_buffer;
  Format format;
  UnreadableClock reference_clock;
};
template <typename ProducerConsumerT>
fpromise::result<StreamSinkInfo, fuchsia_audio_mixer::CreateNodeError>  //
ValidateStreamSink(std::string_view debug_description, std::string_view node_name,
                   ClockRegistry& clock_registry, ClockFactory& clock_factory,
                   ProducerConsumerT& stream_sink, bool writable) {
  bool has_channel;
  if constexpr (std::is_same_v<ProducerConsumerT, fuchsia_audio_mixer::wire::StreamSinkProducer>) {
    has_channel = stream_sink.has_server_end() && stream_sink.server_end().is_valid();
  } else {
    has_channel = stream_sink.has_client_end() && stream_sink.client_end().is_valid();
  }

  if (!has_channel ||               //
      !stream_sink.has_format() ||  //
      !stream_sink.has_reference_clock() || !stream_sink.reference_clock().has_handle() ||
      !stream_sink.has_payload_buffer() || !stream_sink.payload_buffer().is_valid() ||
      !stream_sink.has_media_ticks_per_second_numerator() ||
      !stream_sink.has_media_ticks_per_second_denominator()) {
    FX_LOGS(WARNING) << debug_description << ": missing field";
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
  }

  auto format_result = Format::Create(stream_sink.format());
  if (!format_result.is_ok()) {
    FX_LOGS(WARNING) << debug_description
                     << ": invalid stream sink format: " << format_result.error();
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }

  auto payload_buffer_result = MemoryMappedBuffer::Create(stream_sink.payload_buffer(), writable);
  if (!payload_buffer_result.is_ok()) {
    FX_LOGS(WARNING) << debug_description
                     << ": invalid stream sink payload buffer: " << payload_buffer_result.error();
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }

  if (stream_sink.media_ticks_per_second_numerator() == 0 ||
      stream_sink.media_ticks_per_second_denominator() == 0) {
    FX_LOGS(WARNING) << debug_description << ": invalid stream sink media ticks/second="
                     << stream_sink.media_ticks_per_second_numerator() << "/"
                     << stream_sink.media_ticks_per_second_denominator();
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }

  auto& rc = stream_sink.reference_clock();
  const auto clock_result =
      LookupClock(clock_registry, clock_factory, std::move(rc.handle()),
                  rc.has_domain() ? rc.domain() : Clock::kExternalDomain,
                  rc.has_name() ? std::string(rc.name().get()) : ClockNameFromNodeName(node_name));
  if (!clock_result.is_ok()) {
    FX_LOGS(WARNING) << debug_description << ": invalid clock: " << clock_result.status_string();
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }

  return fpromise::ok(StreamSinkInfo{
      .payload_buffer = std::move(payload_buffer_result.value()),
      .format = format_result.value(),
      .reference_clock = UnreadableClock(clock_result.value()),
  });
}

// Validates `ring_buffer` and translates from FIDL types to internal C++ types.
struct RingBufferInfo {
  std::shared_ptr<RingBuffer> ring_buffer;
  Format format;
  UnreadableClock reference_clock;
};
fpromise::result<RingBufferInfo, fuchsia_audio_mixer::CreateNodeError>  //
ValidateRingBuffer(std::string_view debug_description, std::string_view node_name,
                   ClockRegistry& clock_registry, ClockFactory& clock_factory,
                   fuchsia_audio::wire::RingBuffer& ring_buffer, const bool writable) {
  if (!ring_buffer.has_vmo() || !ring_buffer.vmo().is_valid() ||  //
      !ring_buffer.has_format() ||                                //
      !ring_buffer.has_producer_bytes() ||                        //
      !ring_buffer.has_consumer_bytes() ||                        //
      !ring_buffer.has_reference_clock() || !ring_buffer.reference_clock().is_valid()) {
    FX_LOGS(WARNING) << debug_description << ": missing field";
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
  }

  auto format_result = Format::Create(ring_buffer.format());
  if (!format_result.is_ok()) {
    FX_LOGS(WARNING) << debug_description
                     << ": invalid ring buffer format: " << format_result.error();
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }

  auto mapped_buffer_result = MemoryMappedBuffer::Create(ring_buffer.vmo(), writable);
  if (!mapped_buffer_result.is_ok()) {
    FX_LOGS(WARNING) << debug_description
                     << ": invalid ring buffer vmo: " << mapped_buffer_result.error();
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
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
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }

  if (format.bytes_per_frame() > static_cast<int64_t>(mapped_buffer->content_size())) {
    FX_LOGS(WARNING) << debug_description << ": ring buffer too small for format, content_size="
                     << mapped_buffer->content_size();
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }

  const auto clock_domain = ring_buffer.has_reference_clock_domain()
                                ? ring_buffer.reference_clock_domain()
                                : Clock::kExternalDomain;
  auto clock_result =
      LookupClock(clock_registry, clock_factory, std::move(ring_buffer.reference_clock()),
                  clock_domain, ClockNameFromNodeName(node_name));
  if (!clock_result.is_ok()) {
    FX_LOGS(WARNING) << debug_description << ": invalid clock: " << clock_result.status_string();
    return fpromise::error(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }

  return fpromise::ok(RingBufferInfo{
      .ring_buffer = RingBuffer::Create({
          .format = format,
          .reference_clock = UnreadableClock(clock_result.value()),
          .buffer = std::move(mapped_buffer),
          .producer_frames =
              static_cast<int64_t>(ring_buffer.producer_bytes()) / format.bytes_per_frame(),
          .consumer_frames =
              static_cast<int64_t>(ring_buffer.consumer_bytes()) / format.bytes_per_frame(),
      }),
      .format = format,
      .reference_clock = UnreadableClock(clock_result.value()),
  });
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
      clock_factory_(std::move(args.clock_factory)),
      clock_registry_(std::move(args.clock_registry)) {}

void GraphServer::CreateProducer(CreateProducerRequestView request,
                                 CreateProducerCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateProducer");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_direction() || !request->has_data_source()) {
    FX_LOGS(WARNING) << "CreateProducer: missing field";
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
    return;
  }

  const auto name = NameOrEmpty(*request);
  std::optional<MetaProducerNode::DataSource> source;
  std::optional<Format> format;
  std::optional<UnreadableClock> reference_clock;

  if (request->data_source().is_stream_sink()) {
    auto& stream_sink = request->data_source().stream_sink();
    auto result = ValidateStreamSink("CreateProducer(StreamSink)", name, *clock_registry_,
                                     *clock_factory_, stream_sink, /*writable=*/false);
    if (!result.is_ok()) {
      completer.ReplyError(result.error());
      return;
    }

    reference_clock = result.value().reference_clock;
    format = result.value().format;

    auto server = StreamSinkServer::Create(
        realtime_fidl_thread_, std::move(stream_sink.server_end()),
        StreamSinkServer::Args{
            .format = *format,
            .media_ticks_per_ns = TimelineRate(stream_sink.media_ticks_per_second_numerator(),
                                               stream_sink.media_ticks_per_second_denominator()),
            .payload_buffers = {{0, std::move(result.value().payload_buffer)}},
        });
    AddChildServer(server);
    source = std::move(server);

  } else if (request->data_source().is_ring_buffer()) {
    auto result =
        ValidateRingBuffer("CreateProducer(RingBuffer)", name, *clock_registry_, *clock_factory_,
                           request->data_source().ring_buffer(), /*writable=*/false);
    if (!result.is_ok()) {
      completer.ReplyError(result.error());
      return;
    }

    reference_clock = result.value().reference_clock;
    format = result.value().format;
    source = std::move(result.value().ring_buffer);

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
      .reference_clock = *reference_clock,
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

  if (!request->has_direction() || !request->has_data_source() || !request->has_options() ||
      !request->options().has_thread()) {
    FX_LOGS(WARNING) << "CreateConsumer: missing field";
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
    return;
  }

  auto mix_thread_it = mix_threads_.find(request->options().thread());
  if (mix_thread_it == mix_threads_.end()) {
    FX_LOGS(WARNING) << "CreateConsumer: invalid thread ID";
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
    return;
  }

  auto& mix_thread = mix_thread_it->second;
  const auto name = NameOrEmpty(*request);
  std::shared_ptr<ConsumerStage::Writer> writer;
  std::optional<Format> format;
  std::optional<UnreadableClock> reference_clock;

  if (request->data_source().is_stream_sink()) {
    auto& stream_sink = request->data_source().stream_sink();
    auto result = ValidateStreamSink("CreateConsumer(StreamSink)", name, *clock_registry_,
                                     *clock_factory_, stream_sink, /*writable=*/false);
    if (!result.is_ok()) {
      completer.ReplyError(result.error());
      return;
    }

    reference_clock = result.value().reference_clock;
    format = result.value().format;

    // Packet size defaults to the mix period or the buffer size, whichever is smaller.
    const int64_t frames_per_mix_period = format->integer_frames_per(
        mix_thread->mix_period(), media::TimelineRate::RoundingMode::Floor);
    const int64_t frames_per_payload_buffer =
        static_cast<int64_t>(result.value().payload_buffer->content_size()) /
        format->bytes_per_frame();
    const int64_t frames_per_packet =
        stream_sink.has_frames_per_packet()
            ? stream_sink.frames_per_packet()
            : std::min(frames_per_mix_period, frames_per_payload_buffer);

    const auto packet_queue = std::make_shared<StreamSinkClient::PacketQueue>();
    const auto client = std::make_shared<StreamSinkClient>(StreamSinkClient::Args{
        .format = *format,
        .frames_per_packet = frames_per_packet,
        .client = fidl::WireSharedClient(std::move(stream_sink.client_end()),
                                         realtime_fidl_thread_->dispatcher()),
        .payload_buffers = {{0, std::move(result.value().payload_buffer)}},
        .recycled_packet_queue = packet_queue,
        .thread = realtime_fidl_thread_,
    });

    // This keeps `client` alive implicitly via the callbacks.
    writer = std::make_shared<StreamSinkConsumerWriter>(StreamSinkConsumerWriter::Args{
        .format = *format,
        .media_ticks_per_ns = TimelineRate(stream_sink.media_ticks_per_second_numerator(),
                                           stream_sink.media_ticks_per_second_denominator()),
        .call_put_packet = [client](auto packet) { client->PutPacket(std::move(packet)); },
        .call_end = [client]() { client->End(); },
        .recycled_packet_queue = packet_queue,
    });

  } else if (request->data_source().is_ring_buffer()) {
    auto result =
        ValidateRingBuffer("CreateConsumer(RingBuffer)", name, *clock_registry_, *clock_factory_,
                           request->data_source().ring_buffer(), /*writable=*/true);
    if (!result.is_ok()) {
      completer.ReplyError(result.error());
      return;
    }

    reference_clock = result.value().reference_clock;
    format = result.value().format;
    writer = std::make_shared<RingBufferConsumerWriter>(result.value().ring_buffer);

  } else {
    FX_LOGS(WARNING) << "Unsupported ConsumerDataSource: "
                     << static_cast<int>(request->data_source().Which());
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kUnsupportedOption);
    return;
  }

  const auto id = NextNodeId();
  const auto consumer = ConsumerNode::Create({
      .name = name,
      .pipeline_direction = request->direction(),
      .format = *format,
      .reference_clock = *reference_clock,
      .writer = std::move(writer),
      .thread = mix_thread,
  });
  nodes_[id] = consumer;

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphCreateConsumerResponse::Builder(arena).id(id).Build());
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

  if (!request->has_reference_clock() || !request->has_direction() || !request->has_config()) {
    FX_LOGS(WARNING) << "CreateCustom: missing field";
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
    return;
  }

  // Validate reference clock.
  const auto name = NameOrEmpty(*request);

  auto& rc = request->reference_clock();
  auto clock_result =
      LookupClock(*clock_registry_, *clock_factory_, std::move(rc.handle()), rc.domain(),
                  rc.has_name() ? std::string(rc.name().get()) : ClockNameFromNodeName(name));
  if (!clock_result.is_ok()) {
    FX_LOGS(WARNING) << "CreateCustom: invalid reference clock";
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
    return;
  }

  // Register parent node.
  const auto id = NextNodeId();
  const auto custom =
      CustomNode::Create({.name = name,
                          .reference_clock = UnreadableClock(std::move(clock_result.value())),
                          .pipeline_direction = request->direction(),
                          .config = request->config(),
                          .detached_thread = detached_thread_});
  if (!custom) {
    FX_LOGS(WARNING) << "CreateCustom: failed to create CustomNode";
    completer.ReplyError(fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
    return;
  }
  nodes_[id] = custom;

  // Register built-in child nodes.
  FX_CHECK(custom->child_sources().size() == 1);
  FX_CHECK(custom->child_dests().size() == 1);
  const auto child_source_id = NextNodeId();
  nodes_[child_source_id] = custom->child_sources().front();
  const auto child_dest_id = NextNodeId();
  nodes_[child_dest_id] = custom->child_dests().front();

  fidl::Arena arena;
  fidl::VectorView<NodeId> source_ids(arena, 1);
  source_ids.at(0) = child_source_id;
  fidl::VectorView<NodeId> dest_ids(arena, 1);
  dest_ids.at(0) = child_dest_id;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphCreateCustomResponse::Builder(arena)
          .id(id)
          .node_properties(fuchsia_audio_mixer::wire::CustomNodeProperties::Builder(arena)
                               .source_ids(fidl::ObjectView{arena, source_ids})
                               .dest_ids(fidl::ObjectView{arena, dest_ids})
                               .Build())
          .Build());
}

void GraphServer::DeleteNode(DeleteNodeRequestView request, DeleteNodeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteNode");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_id()) {
    FX_LOGS(WARNING) << "DeleteNode: missing id";
    completer.ReplyError(fuchsia_audio_mixer::DeleteNodeError::kDoesNotExist);
    return;
  }

  auto it = nodes_.find(request->id());
  if (it == nodes_.end()) {
    FX_LOGS(WARNING) << "DeleteNode: invalid id";
    completer.ReplyError(fuchsia_audio_mixer::DeleteNodeError::kDoesNotExist);
    return;
  }

  Node::Destroy(*global_task_queue_, detached_thread_, it->second);
  nodes_.erase(it);

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphDeleteNodeResponse::Builder(arena).Build());
}

void GraphServer::CreateEdge(CreateEdgeRequestView request, CreateEdgeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateEdge");
  ScopedThreadChecker checker(thread().checker());

  // TODO(fxbug.dev/87651): also handle `request->mixer_sampler` and `request->gain_stages` (which
  // should be renamed to `request->gain_controls`)

  if (!request->has_source_id()) {
    FX_LOGS(WARNING) << "CreateEdge: missing source_id";
    completer.ReplyError(fuchsia_audio_mixer::CreateEdgeError::kInvalidSourceId);
    return;
  }
  if (!request->has_dest_id()) {
    FX_LOGS(WARNING) << "CreateEdge: missing dest_id";
    completer.ReplyError(fuchsia_audio_mixer::CreateEdgeError::kInvalidDestId);
    return;
  }

  auto source_it = nodes_.find(request->source_id());
  if (source_it == nodes_.end()) {
    FX_LOGS(WARNING) << "CreateEdge: invalid source_id";
    completer.ReplyError(fuchsia_audio_mixer::CreateEdgeError::kInvalidSourceId);
    return;
  }

  auto dest_it = nodes_.find(request->dest_id());
  if (dest_it == nodes_.end()) {
    FX_LOGS(WARNING) << "CreateEdge: invalid dest_id";
    completer.ReplyError(fuchsia_audio_mixer::CreateEdgeError::kInvalidDestId);
    return;
  }

  auto& source = source_it->second;
  auto& dest = dest_it->second;
  auto result = Node::CreateEdge(*global_task_queue_, source, dest);
  if (!result.is_ok()) {
    completer.ReplyError(result.error());
    return;
  }

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphCreateEdgeResponse::Builder(arena).Build());
}

void GraphServer::DeleteEdge(DeleteEdgeRequestView request, DeleteEdgeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteEdge");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_source_id()) {
    FX_LOGS(WARNING) << "DeleteEdge: missing source_id";
    completer.ReplyError(fuchsia_audio_mixer::DeleteEdgeError::kInvalidSourceId);
    return;
  }
  if (!request->has_dest_id()) {
    FX_LOGS(WARNING) << "DeleteEdge: missing dest_id";
    completer.ReplyError(fuchsia_audio_mixer::DeleteEdgeError::kInvalidDestId);
    return;
  }

  auto source_it = nodes_.find(request->source_id());
  if (source_it == nodes_.end()) {
    FX_LOGS(WARNING) << "DeleteEdge: invalid source_id";
    completer.ReplyError(fuchsia_audio_mixer::DeleteEdgeError::kInvalidSourceId);
    return;
  }

  auto dest_it = nodes_.find(request->dest_id());
  if (dest_it == nodes_.end()) {
    FX_LOGS(WARNING) << "DeleteEdge: invalid dest_id";
    completer.ReplyError(fuchsia_audio_mixer::DeleteEdgeError::kInvalidDestId);
    return;
  }

  auto& source = source_it->second;
  auto& dest = dest_it->second;
  auto result = Node::DeleteEdge(*global_task_queue_, detached_thread_, source, dest);
  if (!result.is_ok()) {
    completer.ReplyError(result.error());
    return;
  }

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphDeleteEdgeResponse::Builder(arena).Build());
}

void GraphServer::CreateThread(CreateThreadRequestView request,
                               CreateThreadCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateThread");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_period() || !request->has_cpu_per_period()) {
    FX_LOGS(WARNING) << "CreateThread: missing field";
    completer.ReplyError(fuchsia_audio_mixer::CreateThreadError::kMissingRequiredField);
    return;
  }

  if (request->period() <= 0 || request->cpu_per_period() <= 0 ||
      request->cpu_per_period() > request->period()) {
    FX_LOGS(WARNING) << "CreateThread: invalid period=" << request->period()
                     << ", cpu_per_period=" << request->cpu_per_period();
    completer.ReplyError(fuchsia_audio_mixer::CreateThreadError::kInvalidParameter);
    return;
  }

  const auto id = NextThreadId();
  mix_threads_[id] = std::make_shared<GraphMixThread>(PipelineMixThread::Args{
      .id = id,
      .name = NameOrEmpty(*request),
      .deadline_profile =
          request->has_deadline_profile() ? std::move(request->deadline_profile()) : zx::profile(),
      .mix_period = zx::nsec(request->period()),
      .cpu_per_period = zx::nsec(request->cpu_per_period()),
      .global_task_queue = global_task_queue_,
      .timer = clock_factory_->CreateTimer(),
      .mono_clock = clock_factory_->SystemMonotonicClock(),
  });

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphCreateThreadResponse::Builder(arena).id(id).Build());
}

void GraphServer::DeleteThread(DeleteThreadRequestView request,
                               DeleteThreadCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteThread");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_id()) {
    FX_LOGS(WARNING) << "DeleteThread: missing `id` field";
    completer.ReplyError(fuchsia_audio_mixer::DeleteThreadError::kInvalidId);
    return;
  }

  auto it = mix_threads_.find(request->id());
  if (it == mix_threads_.end()) {
    FX_LOGS(WARNING) << "DeleteThread: thread " << request->id() << " not found";
    completer.ReplyError(fuchsia_audio_mixer::DeleteThreadError::kInvalidId);
    return;
  }

  auto& mix_thread = it->second;
  if (mix_thread->num_consumers() > 0) {
    FX_LOGS(WARNING) << "DeleteThread: thread " << request->id() << " still in use by "
                     << mix_thread->num_consumers() << " consumers";
    completer.ReplyError(fuchsia_audio_mixer::DeleteThreadError::kStillInUse);
    return;
  }

  // Shutdown this thread and delete it.
  mix_thread->Shutdown();
  mix_threads_.erase(it);

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphDeleteThreadResponse::Builder(arena).Build());
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

  auto name = std::string("GraphControlledClock") + std::to_string(num_graph_controlled_clocks_);
  num_graph_controlled_clocks_++;

  // Create and register.
  auto create_result = clock_factory_->CreateGraphControlledClock(name);
  if (!create_result.is_ok()) {
    completer.ReplyError(create_result.status_value());
    return;
  }

  auto [clock, handle] = std::move(create_result.value());
  clock_registry_->Add(clock);

  // This should not fail.
  zx::eventpair local_fence;
  zx::eventpair remote_fence;
  auto status = zx::eventpair::create(0, &local_fence, &remote_fence);
  FX_CHECK(status == ZX_OK) << "zx::eventpair::create failed with status " << status;

  // To ensure the client can use `handle` until they close `remote_fence`, `clock` must stay in the
  // registry for at least that long. Hence, we hold onto `clock` until the peer of `local_fence` is
  // closed.
  auto wait_it = pending_one_shot_waiters_.emplace(pending_one_shot_waiters_.end(),
                                                   local_fence.get(), ZX_EVENTPAIR_PEER_CLOSED, 0);
  wait_it->Begin(thread().dispatcher(), [this, self = shared_from_this(), clock = std::move(clock),
                                         fence = std::move(local_fence), wait_it](
                                            async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                            zx_status_t status, const zx_packet_signal_t* signal) {
    // TODO(fxbug.dev/87651): need to tell clock_registry_ to stop adjusting `clock`
    pending_one_shot_waiters_.erase(wait_it);
  });

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphCreateGraphControlledReferenceClockResponse::Builder(arena)
          .reference_clock(std::move(handle))
          .release_fence(std::move(remote_fence))
          .Build());
}

void GraphServer::OnShutdown(fidl::UnbindInfo info) {
  // Clearing this list will cancel all pending waiters.
  pending_one_shot_waiters_.clear();

  // Destroy nodes to remove circular references.
  for (auto [id, node] : nodes_) {
    Node::Destroy(*global_task_queue_, detached_thread_, node);
  }
  nodes_.clear();

  // Shutdown all threads.
  for (auto [id, mix_thread] : mix_threads_) {
    mix_thread->Shutdown();
  }
  mix_threads_.clear();

  BaseFidlServer::OnShutdown(info);
}

NodeId GraphServer::NextNodeId() {
  auto id = next_node_id_++;
  FX_CHECK(id != kInvalidId);
  return id;
}

ThreadId GraphServer::NextThreadId() {
  auto id = next_thread_id_++;
  FX_CHECK(id != kInvalidId);
  return id;
}

}  // namespace media_audio
