// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/fidl_processor.h"

#include <lib/syslog/cpp/macros.h>

#include <vector>

#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"
#include "src/media/playback/mediaplayer/fidl/fidl_type_conversions.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/graph/types/audio_stream_type.h"
#include "src/media/playback/mediaplayer/util/safe_clone.h"

namespace media_player {

constexpr uint32_t kOutputIndex = 0;

// static
void FidlProcessor::Create(ServiceProvider* service_provider, StreamType::Medium medium,
                           Function function, fuchsia::media::StreamProcessorPtr processor,
                           fit::function<void(std::shared_ptr<Processor>)> callback) {
  auto fidl_processor = std::make_shared<FidlProcessor>(service_provider, medium, function);
  fidl_processor->Init(std::move(processor),
                       [fidl_processor, callback = std::move(callback)](bool succeeded) {
                         callback(succeeded ? fidl_processor : nullptr);
                       });
}

// static
std::shared_ptr<Processor> FidlProcessor::Create(ServiceProvider* service_provider,
                                                 StreamType::Medium medium, Function function,
                                                 fuchsia::media::StreamProcessorPtr processor) {
  auto fidl_processor = std::make_shared<FidlProcessor>(service_provider, medium, function);
  fidl_processor->Init(std::move(processor), nullptr);
  return fidl_processor;
}

FidlProcessor::FidlProcessor(ServiceProvider* service_provider, StreamType::Medium medium,
                             Function function)
    : service_provider_(service_provider), medium_(medium), function_(function) {
  switch (medium_) {
    case StreamType::Medium::kAudio:
      output_stream_type_ =
          AudioStreamType::Create(nullptr, StreamType::kAudioEncodingLpcm, nullptr,
                                  AudioStreamType::SampleFormat::kNone, 1, 1);
      break;
    case StreamType::Medium::kVideo:
      output_stream_type_ =
          VideoStreamType::Create(nullptr, StreamType::kVideoEncodingUncompressed, nullptr,
                                  VideoStreamType::PixelFormat::kUnknown,
                                  VideoStreamType::ColorSpace::kUnknown, 0, 0, 0, 0, 1, 1, 0);
      break;
    case StreamType::Medium::kText:
    case StreamType::Medium::kSubpicture:
      FX_CHECK(false) << "Only audio and video are supported.";
      break;
  }
}

void FidlProcessor::Init(fuchsia::media::StreamProcessorPtr processor,
                         fit::function<void(bool)> callback) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
  FX_DCHECK(processor);

  outboard_processor_ = std::move(processor);
  init_callback_ = std::move(callback);

  outboard_processor_.set_error_handler(fit::bind_member(this, &FidlProcessor::OnConnectionFailed));

  outboard_processor_.events().OnStreamFailed =
      fit::bind_member(this, &FidlProcessor::OnStreamFailed);
  outboard_processor_.events().OnInputConstraints =
      fit::bind_member(this, &FidlProcessor::OnInputConstraints);
  outboard_processor_.events().OnOutputConstraints =
      fit::bind_member(this, &FidlProcessor::OnOutputConstraints);
  outboard_processor_.events().OnOutputFormat =
      fit::bind_member(this, &FidlProcessor::OnOutputFormat);
  outboard_processor_.events().OnOutputPacket =
      fit::bind_member(this, &FidlProcessor::OnOutputPacket);
  outboard_processor_.events().OnOutputEndOfStream =
      fit::bind_member(this, &FidlProcessor::OnOutputEndOfStream);
  outboard_processor_.events().OnFreeInputPacket =
      fit::bind_member(this, &FidlProcessor::OnFreeInputPacket);

  outboard_processor_->EnableOnStreamFailed();
}

FidlProcessor::~FidlProcessor() { FIT_DCHECK_IS_THREAD_VALID(thread_checker_); }

const char* FidlProcessor::label() const {
  switch (function_) {
    case Function::kDecode:
      switch (medium_) {
        case StreamType::Medium::kAudio:
          return "fidl audio decoder";
        case StreamType::Medium::kVideo:
          return "fidl video decoder";
        case StreamType::Medium::kText:
          return "fidl text decoder";
        case StreamType::Medium::kSubpicture:
          return "fidl subpicture decoder";
      }
      break;
    case Function::kDecrypt:
      switch (medium_) {
        case StreamType::Medium::kAudio:
          return "fidl audio decryptor";
        case StreamType::Medium::kVideo:
          return "fidl video decryptor";
        case StreamType::Medium::kText:
          return "fidl text decryptor";
        case StreamType::Medium::kSubpicture:
          return "fidl subpicture decryptor";
      }
      break;
  }
}

void FidlProcessor::Dump(std::ostream& os) const {
  os << label() << fostr::Indent;
  Node::Dump(os);
  // TODO(dalesat): More.
  os << fostr::Outdent;
}

void FidlProcessor::ConfigureConnectors() {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  ConfigureInputDeferred();
  ConfigureOutputDeferred();
}

void FidlProcessor::OnInputConnectionReady(size_t input_index) {
  FX_DCHECK(input_index == 0);
  FX_DCHECK(input_buffers_.has_current_set());
  BufferSet& current_set = input_buffers_.current_set();
  current_set.SetBufferCount(UseInputVmos().GetVmos().size());
}

void FidlProcessor::FlushInput(bool hold_frame, size_t input_index, fit::closure callback) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
  FX_DCHECK(input_index == 0);
  FX_DCHECK(callback);

  // This processor will always receive a FlushOutput shortly after a FlushInput.
  // We call CloseCurrentStream now to let the outboard processor know we're
  // abandoning this stream. Incrementing stream_lifetime_ordinal_ will cause
  // any stale output packets to be discarded. When FlushOutput is called, we'll
  // sync with the outboard processor to make sure we're all caught up.
  outboard_processor_->CloseCurrentStream(stream_lifetime_ordinal_, false, false);
  stream_lifetime_ordinal_ += 2;
  end_of_input_stream_ = false;
  flushing_ = true;

  callback();
}

void FidlProcessor::PutInputPacket(PacketPtr packet, size_t input_index) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
  FX_DCHECK(packet);
  FX_DCHECK(input_index == 0);
  FX_DCHECK(input_buffers_.has_current_set());

  if (flushing_) {
    return;
  }

  if (pts_rate_ == media::TimelineRate()) {
    pts_rate_ = packet->pts_rate();
  } else {
    FX_DCHECK(pts_rate_ == packet->pts_rate());
  }

  if (packet->size() != 0) {
    // The buffer attached to this packet will be one we created using
    // |input_buffers_|.

    BufferSet& current_set = input_buffers_.current_set();

    FX_DCHECK(packet->payload_buffer()->id() < current_set.buffer_count())
        << "Buffer ID " << packet->payload_buffer()->id()
        << " is out of range, should be less than " << current_set.buffer_count();
    current_set.AddRefBufferForProcessor(packet->payload_buffer()->id(), packet->payload_buffer());

    fuchsia::media::Packet codec_packet;
    codec_packet.mutable_header()->set_buffer_lifetime_ordinal(current_set.lifetime_ordinal());
    codec_packet.mutable_header()->set_packet_index(packet->payload_buffer()->id());
    codec_packet.set_buffer_index(packet->payload_buffer()->id());
    codec_packet.set_stream_lifetime_ordinal(stream_lifetime_ordinal_);
    codec_packet.set_start_offset(0);
    codec_packet.set_valid_length_bytes(packet->size());
    codec_packet.set_timestamp_ish(static_cast<uint64_t>(packet->pts()));
    codec_packet.set_start_access_unit(packet->keyframe());
    codec_packet.set_known_end_access_unit(false);

    FX_DCHECK(packet->size() <= current_set.buffer_size());

    outboard_processor_->QueueInputPacket(std::move(codec_packet));
  }

  if (packet->end_of_stream()) {
    end_of_input_stream_ = true;
    outboard_processor_->QueueInputEndOfStream(stream_lifetime_ordinal_);
  }
}

void FidlProcessor::OnOutputConnectionReady(size_t output_index) {
  FX_DCHECK(output_index == 0);

  if (allocate_output_buffers_for_processor_pending_) {
    allocate_output_buffers_for_processor_pending_ = false;
    // We allocate all the buffers on behalf of the outboard processor. We give
    // the outboard processor ownership of these buffers as long as this set is
    // current. The processor decides what buffers to use for output. When an
    // output packet is produced, the player shares ownership of the buffer until
    // all packets referencing the buffer are recycled. This ownership model
    // reflects the fact that the outboard processor is free to use output buffers
    // as references and even use the same output buffer for multiple packets as
    // happens with VP9.
    FX_DCHECK(output_buffers_.has_current_set());
    BufferSet& current_set = output_buffers_.current_set();
    current_set.SetBufferCount(UseOutputVmos().GetVmos().size());
    current_set.AllocateAllBuffersForProcessor(UseOutputVmos());
  }
}

void FidlProcessor::FlushOutput(size_t output_index, fit::closure callback) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
  FX_DCHECK(output_index == 0);
  FX_DCHECK(callback);

  // This processor will always receive a FlushInput shortly before a FlushOutput.
  // In FlushInput, we've already closed the stream. Now we sync with the
  // output processor just to make sure we're caught up.
  outboard_processor_->Sync(std::move(callback));
}

void FidlProcessor::RequestOutputPacket() {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
  flushing_ = false;

  MaybeRequestInputPacket();
}

void FidlProcessor::SetInputStreamType(const StreamType& stream_type) {
  FX_DCHECK(stream_type.medium() == medium_);

  if (function_ == Function::kDecode) {
    // Decoders know their input stream type when they come from the factory.
    return;
  }

  FX_DCHECK(stream_type.encrypted());

  switch (medium_) {
    case StreamType::Medium::kAudio: {
      auto& t = *stream_type.audio();
      output_stream_type_ =
          AudioStreamType::Create(nullptr, t.encoding(), SafeClone(t.encoding_parameters()),
                                  t.sample_format(), t.channels(), t.frames_per_second());
    } break;
    case StreamType::Medium::kVideo: {
      auto& t = *stream_type.video();
      output_stream_type_ = VideoStreamType::Create(
          nullptr, t.encoding(), SafeClone(t.encoding_parameters()), t.pixel_format(),
          t.color_space(), t.width(), t.height(), t.coded_width(), t.coded_height(),
          t.pixel_aspect_ratio_width(), t.pixel_aspect_ratio_height(), t.line_stride());
    } break;
    case StreamType::Medium::kText:
    case StreamType::Medium::kSubpicture:
      FX_CHECK(false) << "Only audio and video are supported.";
      break;
  }
}

std::unique_ptr<StreamType> FidlProcessor::output_stream_type() const {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
  FX_DCHECK(output_stream_type_);
  return output_stream_type_->Clone();
}

void FidlProcessor::InitSucceeded() {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (init_callback_) {
    auto callback = std::move(init_callback_);
    callback(true);
  }
}

void FidlProcessor::InitFailed() {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (init_callback_) {
    auto callback = std::move(init_callback_);
    callback(false);
  }
}

void FidlProcessor::MaybeRequestInputPacket() {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (!flushing_ && input_buffers_.has_current_set() && !end_of_input_stream_) {
    // |HasFreeBuffer| returns true if there's a free buffer. If there's no
    // free buffer, it will call the callback when there is one.
    if (!input_buffers_.current_set().HasFreeBuffer(
            [this]() { PostTask([this]() { MaybeRequestInputPacket(); }); })) {
      return;
    }

    RequestInputPacket();
  }
}

void FidlProcessor::OnConnectionFailed(zx_status_t error) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  FX_PLOGS(ERROR, error) << "OnConnectionFailed";

  InitFailed();
  // TODO(dalesat): Report failure.
}

void FidlProcessor::OnStreamFailed(uint64_t stream_lifetime_ordinal,
                                   fuchsia::media::StreamError error) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
  FX_LOGS(ERROR) << "OnStreamFailed: stream_lifetime_ordinal: " << stream_lifetime_ordinal
                 << " error: " << std::hex << static_cast<uint32_t>(error);
  // TODO(dalesat): Report failure.
}

void FidlProcessor::OnInputConstraints(fuchsia::media::StreamBufferConstraints constraints) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
  FX_DCHECK(!input_buffers_.has_current_set()) << "OnInputConstraints received more than once.";

  input_buffers_.ApplyConstraints(constraints, true);
  FX_DCHECK(input_buffers_.has_current_set());
  BufferSet& current_set = input_buffers_.current_set();

  ConfigureInputToUseSysmemVmos(
      service_provider_, 0,  // max_aggregate_payload_size
      current_set.packet_count_for_server(), current_set.buffer_size(),
      current_set.single_vmo() ? VmoAllocation::kSingleVmo : VmoAllocation::kVmoPerBuffer,
      0,  // map_flags
      [&current_set](uint64_t size, const PayloadVmos& payload_vmos) {
        // This callback runs on an arbitrary thread.
        return current_set.AllocateBuffer(size, payload_vmos);
      });

  // Call |Sync| on the sysmem token before passing it to the outboard processor as part of
  // |SetInputBufferPartialSettings|. This needs to done to ensure that sysmem recognizes the
  // token when it arrives. The outboard processor doesn't do this.
  input_sysmem_token_ = TakeInputSysmemToken();
  // TODO(dalesat): Use the BufferCollection::Sync() instead, since token Sync() may go away before
  // long.
  input_sysmem_token_->Sync([this, &current_set]() {
    outboard_processor_->SetInputBufferPartialSettings(
        current_set.PartialSettings(std::move(input_sysmem_token_)));
    InitSucceeded();
  });
}

void FidlProcessor::OnOutputConstraints(fuchsia::media::StreamOutputConstraints constraints) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (constraints.has_buffer_constraints_action_required() &&
      constraints.buffer_constraints_action_required() && !constraints.has_buffer_constraints()) {
    FX_LOGS(ERROR) << "OnOutputConstraints: constraints action required but constraints missing";
    InitFailed();
    return;
  }

  if (!constraints.has_buffer_constraints_action_required() ||
      !constraints.buffer_constraints_action_required()) {
    if (init_callback_) {
      FX_LOGS(ERROR)
          << "OnOutputConstraints: constraints action not required on initial constraints.";
      InitFailed();
      return;
    }
  }

  if (output_buffers_.has_current_set()) {
    // All the old output buffers were owned by the outboard processor. We release
    // that ownership. The buffers will continue to exist until all packets
    // referencing them are destroyed.
    output_buffers_.current_set().ReleaseAllProcessorOwnedBuffers();
  }

  // Use a single VMO for audio, VMO per buffer for video.
  const bool success = output_buffers_.ApplyConstraints(constraints.buffer_constraints(),
                                                        medium_ == StreamType::Medium::kAudio);
  if (!success) {
    FX_LOGS(ERROR) << "OnOutputConstraints: Failed to apply constraints.";
    InitFailed();
    return;
  }

  FX_DCHECK(output_buffers_.has_current_set());
  BufferSet& current_set = output_buffers_.current_set();

  ConfigureOutputToUseSysmemVmos(
      service_provider_, 0,  // max_aggregate_payload_size
      current_set.packet_count_for_server(), current_set.buffer_size(),
      current_set.single_vmo() ? VmoAllocation::kSingleVmo : VmoAllocation::kVmoPerBuffer,
      0);  // map_flags

  // Call |Sync| on the sysmem token before passing it to the outboard processor as part of
  // |SetOutputBufferPartialSettings|. This needs to be done to ensure that sysmem recognizes the
  // token when it arrives. The outboard processor doesn't do this.
  output_sysmem_token_ = TakeOutputSysmemToken();
  // TODO(dalesat): Use the BufferCollection::Sync() instead, since token Sync() may go away before
  // long.
  output_sysmem_token_->Sync([this]() {
    BufferSet& current_set = output_buffers_.current_set();
    outboard_processor_->SetOutputBufferPartialSettings(
        current_set.PartialSettings(std::move(output_sysmem_token_)));

    outboard_processor_->CompleteOutputBufferPartialSettings(current_set.lifetime_ordinal());

    allocate_output_buffers_for_processor_pending_ = true;
    if (OutputConnectionReady()) {
      OnOutputConnectionReady(kOutputIndex);
    }
  });
}

void FidlProcessor::OnOutputFormat(fuchsia::media::StreamOutputFormat format) {
  if (!format.has_format_details()) {
    FX_LOGS(ERROR) << "Config has no format details.";
    InitFailed();
    return;
  }

  auto stream_type = fidl::To<std::unique_ptr<StreamType>>(format.format_details());
  if (!stream_type) {
    FX_LOGS(ERROR) << "Can't comprehend format details.";
    InitFailed();
    return;
  }

  if (!format.format_details().has_format_details_version_ordinal()) {
    FX_LOGS(ERROR) << "Format details do not have version ordinal.";
    InitFailed();
    return;
  }

  if (output_stream_type_) {
    if (output_format_details_version_ordinal_ !=
        format.format_details().format_details_version_ordinal()) {
      HandlePossibleOutputStreamTypeChange(*output_stream_type_, *stream_type);
    }
  }

  output_format_details_version_ordinal_ = format.format_details().format_details_version_ordinal();

  output_stream_type_ = std::move(stream_type);
  have_real_output_stream_type_ = true;
}

void FidlProcessor::OnOutputPacket(fuchsia::media::Packet packet, bool error_detected_before,
                                   bool error_detected_during) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (!packet.has_header() || !packet.header().has_buffer_lifetime_ordinal() ||
      !packet.header().has_packet_index() || !packet.has_buffer_index() ||
      !packet.has_valid_length_bytes() || !packet.has_stream_lifetime_ordinal()) {
    FX_LOGS(ERROR) << "Packet not fully initialized.";
    return;
  }

  uint64_t buffer_lifetime_ordinal = packet.header().buffer_lifetime_ordinal();
  uint32_t packet_index = packet.header().packet_index();
  uint32_t buffer_index = packet.buffer_index();
  FX_DCHECK(buffer_index != 0x80000000);

  if (error_detected_before) {
    FX_LOGS(WARNING) << "OnOutputPacket: error_detected_before";
  }

  if (error_detected_during) {
    FX_LOGS(WARNING) << "OnOutputPacket: error_detected_during";
  }

  if (!output_buffers_.has_current_set()) {
    FX_LOGS(FATAL) << "OnOutputPacket event without prior OnOutputConstraints event";
    // TODO(dalesat): Report error rather than crashing.
  }

  if (!have_real_output_stream_type_) {
    FX_LOGS(FATAL) << "OnOutputPacket event without prior OnOutputFormat event";
    // TODO(dalesat): Report error rather than crashing.
  }

  BufferSet& current_set = output_buffers_.current_set();

  if (packet.header().buffer_lifetime_ordinal() != current_set.lifetime_ordinal()) {
    // Refers to an obsolete buffer. We've already assumed the outboard
    // processor gave up this buffer, so there's no need to free it. Also, this
    // shouldn't happen, and there's no evidence that it does.
    FX_LOGS(FATAL) << "OnOutputPacket delivered tacket with obsolete "
                      "buffer_lifetime_ordinal.";
    return;
  }

  if (packet.stream_lifetime_ordinal() != stream_lifetime_ordinal_) {
    // Refers to an obsolete stream. We'll just recycle the packet back to the
    // output processor.
    outboard_processor_->RecycleOutputPacket(std::move(*packet.mutable_header()));
    return;
  }

  // All the output buffers in the current set are always owned by the outboard
  // processor. Get another reference to the |PayloadBuffer| for the specified
  // buffer.
  FX_DCHECK(buffer_lifetime_ordinal == current_set.lifetime_ordinal());
  fbl::RefPtr<PayloadBuffer> payload_buffer = current_set.GetProcessorOwnedBuffer(buffer_index);

  // TODO(dalesat): Tolerate !has_timestamp_ish somehow.
  if (!packet.has_timestamp_ish()) {
    FX_LOGS(ERROR) << "We demand has_timestamp_ish for now (TODO)";
    return;
  }

  next_pts_ = static_cast<int64_t>(packet.timestamp_ish());

  auto output_packet = Packet::Create(next_pts_, pts_rate_, true, false,
                                      packet.valid_length_bytes(), std::move(payload_buffer));

  if (revised_output_stream_type_) {
    output_packet->SetRevisedStreamType(std::move(revised_output_stream_type_));
  }

  output_packet->AfterRecycling(
      [this, shared_this = shared_from_this(), packet_index](Packet* packet) {
        PostTask([this, shared_this, packet_index,
                  buffer_lifetime_ordinal = packet->payload_buffer()->buffer_config()]() {
          FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

          // |outboard_processor_| is always set after |Init| is called, so we
          // can rely on it here.
          FX_DCHECK(outboard_processor_);
          fuchsia::media::PacketHeader header;
          header.set_buffer_lifetime_ordinal(buffer_lifetime_ordinal);
          header.set_packet_index(packet_index);
          outboard_processor_->RecycleOutputPacket(std::move(header));
        });
      });

  PutOutputPacket(std::move(output_packet));
}

void FidlProcessor::OnOutputEndOfStream(uint64_t stream_lifetime_ordinal,
                                        bool error_detected_before) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (error_detected_before) {
    FX_LOGS(WARNING) << "OnOutputEndOfStream: error_detected_before";
  }

  PutOutputPacket(Packet::CreateEndOfStream(next_pts_, pts_rate_));
}

void FidlProcessor::OnFreeInputPacket(fuchsia::media::PacketHeader packet_header) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (!packet_header.has_buffer_lifetime_ordinal() || !packet_header.has_packet_index()) {
    FX_LOGS(ERROR) << "Freed packet missing ordinal or index.";
    return;
  }

  input_buffers_.ReleaseBufferForProcessor(packet_header.buffer_lifetime_ordinal(),
                                           packet_header.packet_index());
}

void FidlProcessor::HandlePossibleOutputStreamTypeChange(const StreamType& old_type,
                                                         const StreamType& new_type) {
  // TODO(dalesat): Actually compare the types.
  revised_output_stream_type_ = new_type.Clone();
}

}  // namespace media_player
