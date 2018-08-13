// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/fidl/fidl_decoder.h"

#include <vector>

#include "garnet/bin/mediaplayer/fidl/fidl_type_conversions.h"
#include "garnet/bin/mediaplayer/framework/formatting.h"
#include "garnet/bin/mediaplayer/framework/types/audio_stream_type.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"

namespace media_player {
namespace {

static constexpr uint8_t kCodec = 1;
static constexpr uint8_t kOther = 2;

static const char kAacAdtsMimeType[] = "audio/aac-adts";

// Creates codec_oob_bytes from a packet payload of at least 4 bytes.
std::vector<uint8_t> MakeOobBytesFromAdtsHeader(const uint8_t* adts_header) {
  std::vector<uint8_t> asc(2);

  // TODO(dustingreen): Switch from ADTS to .mp4 and fix AAC decoder to not
  // require "AudioSpecificConfig()" when fed ADTS.  In other words, move the
  // stuff here into a shim around the AAC OMX decoder, just next to (above or
  // below) the OmxCodecRunner in the codec_runner_sw_omx isolate, probably.

  // For SoftAAC2.cpp, for no particularly good reason, a CODECCONFIG buffer is
  // expected, even when running in ADTS mode, despite all the relevant data
  // being available from the ADTS header.  The CODECCONFIG buffer has an
  // AudioSpecificConfig in it.  The AudioSpecificConfig has to be created based
  // on corresponding fields of the ADTS header - not that requiring this of
  // the codec client makes any sense whatsoever...
  //
  // TODO(dustingreen): maybe add a per-codec compensation layer to un-crazy the
  // quirks of each codec.  For example, when decoding ADTS, all the needed info
  // is there in the ADTS stream directly.  No reason to hassle the codec client
  // for a pointless translated form of the same info.  In contrast, when it's
  // an mp4 file (or mkv, or whatever modern container format), the codec config
  // info is relevant.  But we should only force a client to provide it if
  // it's really needed.

  uint8_t profile_ObjectType;        // name in AAC spec in adts_fixed_header
  uint8_t sampling_frequency_index;  // name in AAC spec in adts_fixed_header
  uint8_t channel_configuration;     // name in AAC spec in adts_fixed_header
  profile_ObjectType = (adts_header[2] >> 6) & 0x3;
  sampling_frequency_index = (adts_header[2] >> 2) & 0xf;
  FXL_DCHECK(sampling_frequency_index < 11);
  channel_configuration = (adts_header[2] & 0x1) << 2 | (adts_header[3] >> 6);

  // Now let's convert these to the forms needed by AudioSpecificConfig.
  uint8_t audioObjectType =
      profile_ObjectType + 1;  // see near Table 1.A.11, for AAC not MPEG-2
  uint8_t samplingFrequencyIndex =
      sampling_frequency_index;                          // no conversion needed
  uint8_t channelConfiguration = channel_configuration;  // no conversion needed
  uint8_t frameLengthFlag = 0;
  uint8_t dependsOnCoreCoder = 0;
  uint8_t extensionFlag = 0;

  // Now we are ready to build a two-byte AudioSpecificConfig.  Not an
  // AudioSpecificInfo as stated in avc_utils.cpp (AOSP) mind you, but an
  // AudioSpecificConfig.
  asc[0] = (audioObjectType << 3) | (samplingFrequencyIndex >> 1);
  asc[1] = (samplingFrequencyIndex << 7) | (channelConfiguration << 3) |
           (frameLengthFlag << 2) | (dependsOnCoreCoder << 1) |
           (extensionFlag << 0);

  return asc;
}

}  // namespace

// static
void FidlDecoder::Create(
    fuchsia::mediacodec::CodecFormatDetails input_format_details,
    fuchsia::mediacodec::CodecPtr decoder,
    fit::function<void(std::shared_ptr<Decoder>)> callback) {
  auto fidl_decoder =
      std::make_shared<FidlDecoder>(std::move(input_format_details));
  fidl_decoder->Init(
      std::move(decoder),
      [fidl_decoder, callback = std::move(callback)](bool succeeded) {
        callback(succeeded ? fidl_decoder : nullptr);
      });
}

FidlDecoder::FidlDecoder(
    fuchsia::mediacodec::CodecFormatDetails input_format_details)
    : input_format_details_(std::move(input_format_details)) {
  update_oob_bytes_ = (input_format_details_.mime_type == kAacAdtsMimeType);
}

void FidlDecoder::Init(fuchsia::mediacodec::CodecPtr decoder,
                       fit::function<void(bool)> callback) {
  FXL_DCHECK(decoder);
  FXL_DCHECK(callback);

  outboard_decoder_ = std::move(decoder);
  init_callback_ = std::move(callback);

  outboard_decoder_.set_error_handler(
      fit::bind_member(this, &FidlDecoder::OnConnectionFailed));

  outboard_decoder_.events().OnStreamFailed =
      fit::bind_member(this, &FidlDecoder::OnStreamFailed);
  outboard_decoder_.events().OnInputConstraints =
      fit::bind_member(this, &FidlDecoder::OnInputConstraints);
  outboard_decoder_.events().OnOutputConfig =
      fit::bind_member(this, &FidlDecoder::OnOutputConfig);
  outboard_decoder_.events().OnOutputPacket =
      fit::bind_member(this, &FidlDecoder::OnOutputPacket);
  outboard_decoder_.events().OnOutputEndOfStream =
      fit::bind_member(this, &FidlDecoder::OnOutputEndOfStream);
  outboard_decoder_.events().OnFreeInputPacket =
      fit::bind_member(this, &FidlDecoder::OnFreeInputPacket);

  outboard_decoder_->EnableOnStreamFailed();
}

FidlDecoder::~FidlDecoder() {}

const char* FidlDecoder::label() const { return "fidl decoder"; }

void FidlDecoder::Dump(std::ostream& os) const {
  GenericNode::Dump(os);
  // TODO(dalesat): More.
}

void FidlDecoder::GetConfiguration(size_t* input_count, size_t* output_count) {
  FXL_DCHECK(input_count);
  FXL_DCHECK(output_count);
  *input_count = 1;
  *output_count = 1;
}

void FidlDecoder::FlushInput(bool hold_frame, size_t input_index,
                             fit::closure callback) {
  FXL_DCHECK(input_index == 0);
  FXL_DCHECK(callback);

  // This decoder will always receive a FlushOutput shortly after a FlushInput.
  // We call CloseCurrentStream now to let the outboard decoder know we're
  // abandoning this stream. Incrementing stream_lifetime_ordinal_ will cause
  // any stale output packets to be discarded. When FlushOutput is called, we'll
  // sync with the outboard decoder to make sure we're all caught up.
  outboard_decoder_->CloseCurrentStream(stream_lifetime_ordinal_, false, false);
  stream_lifetime_ordinal_ += 2;
  end_of_input_stream_ = false;
  update_oob_bytes_ = (input_format_details_.mime_type == kAacAdtsMimeType);
  flushing_ = true;
  callback();
}

std::shared_ptr<PayloadAllocator> FidlDecoder::allocator_for_input(
    size_t input_index) {
  FXL_DCHECK(input_index == 0);
  return nullptr;
}

void FidlDecoder::PutInputPacket(PacketPtr packet, size_t input_index) {
  FXL_DCHECK(packet);
  FXL_DCHECK(input_index == 0);
  FXL_DCHECK(input_buffers_.has_current_set());

  if (flushing_) {
    return;
  }

  if (pts_rate_ == media::TimelineRate()) {
    pts_rate_ = packet->pts_rate();
  } else {
    FXL_DCHECK(pts_rate_ == packet->pts_rate());
  }

  if (packet->size() != 0) {
    BufferSet& current_set = input_buffers_.current_set();

    FXL_DCHECK(current_set.free_buffer_count() != 0);

    // TODO(dalesat): Remove when the aac/adts decoder no longer needs this
    // help.
    if (update_oob_bytes_ && packet->size() >= 4) {
      FXL_DCHECK(packet->payload());
      input_format_details_.codec_oob_bytes =
          fidl::VectorPtr<uint8_t>(MakeOobBytesFromAdtsHeader(
              static_cast<const uint8_t*>(packet->payload())));
      outboard_decoder_->QueueInputFormatDetails(
          stream_lifetime_ordinal_, fidl::Clone(input_format_details_));
      update_oob_bytes_ = false;
    }

    uint32_t buffer_index = current_set.AllocateBuffer(kCodec);

    fuchsia::mediacodec::CodecPacket codec_packet;
    codec_packet.header.buffer_lifetime_ordinal =
        current_set.lifetime_ordinal();
    codec_packet.header.packet_index = buffer_index;
    codec_packet.stream_lifetime_ordinal = stream_lifetime_ordinal_;
    codec_packet.start_offset = 0;
    codec_packet.valid_length_bytes = packet->size();
    codec_packet.timestamp_ish = static_cast<uint64_t>(packet->pts());
    codec_packet.start_access_unit = packet->keyframe();
    codec_packet.known_end_access_unit = false;

    FXL_DCHECK(packet->size() <= current_set.buffer_size());

    void* payload = current_set.GetBufferData(buffer_index);
    std::memcpy(payload, packet->payload(), packet->size());

    outboard_decoder_->QueueInputPacket(std::move(codec_packet));
  }

  if (packet->end_of_stream()) {
    end_of_input_stream_ = true;
    outboard_decoder_->QueueInputEndOfStream(stream_lifetime_ordinal_);
  }
}

void FidlDecoder::FlushOutput(size_t output_index, fit::closure callback) {
  FXL_DCHECK(output_index == 0);
  FXL_DCHECK(callback);

  // This decoder will always receive a FlushInput shortly before a FlushOutput.
  // In FlushInput, we've already closed the stream. Now we sync with the
  // output decoder just to make sure we're caught up.
  outboard_decoder_->Sync(std::move(callback));
}

void FidlDecoder::RequestOutputPacket() {
  flushing_ = false;

  if (input_buffers_.has_current_set() &&
      input_buffers_.current_set().free_buffer_count() != 0 &&
      !end_of_input_stream_) {
    stage()->RequestInputPacket();
  }
}

std::unique_ptr<StreamType> FidlDecoder::output_stream_type() const {
  FXL_DCHECK(stream_type_);
  return stream_type_->Clone();
}

void FidlDecoder::InitSucceeded() {
  if (init_callback_) {
    init_callback_(true);
    init_callback_ = nullptr;
  }
}

void FidlDecoder::InitFailed() {
  if (init_callback_) {
    init_callback_(false);
    init_callback_ = nullptr;
  }
}

void FidlDecoder::OnConnectionFailed() {
  InitFailed();
  // TODO(dalesat): Report failure.
}

void FidlDecoder::OnStreamFailed(uint64_t stream_lifetime_ordinal) {
  // TODO(dalesat): Report failure.
}

void FidlDecoder::OnInputConstraints(
    fuchsia::mediacodec::CodecBufferConstraints constraints) {
  if (input_buffers_.has_current_set()) {
    input_buffers_.current_set().FreeAllBuffersOwnedBy(kCodec);
  }

  input_buffers_.ApplyConstraints(constraints);
  FXL_DCHECK(input_buffers_.has_current_set());
  BufferSet& current_set = input_buffers_.current_set();

  outboard_decoder_->SetInputBufferSettings(current_set.settings());

  for (uint32_t index = 0; index < current_set.buffer_count(); ++index) {
    outboard_decoder_->AddInputBuffer(
        current_set.GetBufferDescriptor(index, false));
  }
}

void FidlDecoder::OnOutputConfig(
    fuchsia::mediacodec::CodecOutputConfig config) {
  auto stream_type =
      fxl::To<std::unique_ptr<StreamType>>(config.format_details);
  if (!stream_type) {
    FXL_LOG(ERROR) << "Can't comprehend format details.";
    InitFailed();
    return;
  }

  if (stream_type_) {
    if (output_format_details_version_ordinal_ !=
        config.format_details.format_details_version_ordinal) {
      HandlePossibleOutputStreamTypeChange(*stream_type_, *stream_type);
    }
  }

  output_format_details_version_ordinal_ =
      config.format_details.format_details_version_ordinal;

  stream_type_ = std::move(stream_type);
  InitSucceeded();

  if (!config.buffer_constraints_action_required) {
    return;
  }

  if (output_buffers_.has_current_set()) {
    output_buffers_.current_set().FreeAllBuffersOwnedBy(kCodec);
  }

  output_buffers_.ApplyConstraints(config.buffer_constraints);

  FXL_DCHECK(output_buffers_.has_current_set());
  BufferSet& current_set = output_buffers_.current_set();

  current_set.AllocateAllFreeBuffers(kCodec);

  outboard_decoder_->SetOutputBufferSettings(current_set.settings());

  for (uint32_t index = 0; index < current_set.buffer_count(); ++index) {
    outboard_decoder_->AddOutputBuffer(
        current_set.GetBufferDescriptor(index, true));
  }
};

void FidlDecoder::OnOutputPacket(fuchsia::mediacodec::CodecPacket packet,
                                 bool error_detected_before,
                                 bool error_detected_during) {
  uint64_t buffer_lifetime_ordinal = packet.header.buffer_lifetime_ordinal;
  uint32_t buffer_index = packet.header.packet_index;

  if (error_detected_before) {
    FXL_LOG(WARNING) << "OnOutputPacket: error_detected_before";
  }

  if (error_detected_during) {
    FXL_LOG(WARNING) << "OnOutputPacket: error_detected_during";
  }

  if (!output_buffers_.has_current_set()) {
    FXL_LOG(FATAL) << "OnOutputPacket event without prior OnOutputConfig event";
    // TODO(dalesat): Report error rather than crashing.
  }

  BufferSet& current_set = output_buffers_.current_set();

  if (packet.header.buffer_lifetime_ordinal != current_set.lifetime_ordinal()) {
    // Refers to an obsolete buffer. We've already assumed the outboard decoder
    // gave up this buffer, so there's no need to free it.
    return;
  }

  if (packet.stream_lifetime_ordinal != stream_lifetime_ordinal_) {
    // Refers to an obsolete stream. We'll just recycle the packet back to the
    // output decoder.
    outboard_decoder_->RecycleOutputPacket(std::move(packet.header));
    return;
  }

  FXL_DCHECK(buffer_lifetime_ordinal == current_set.lifetime_ordinal());
  current_set.TransferBuffer(buffer_index, kOther);

  void* payload = reinterpret_cast<void*>(
      reinterpret_cast<uint8_t*>(
          current_set.GetBufferData(packet.header.packet_index)) +
      packet.start_offset);

  next_pts_ = static_cast<int64_t>(packet.timestamp_ish);

  auto decoder_packet = DecoderPacket::Create(
      next_pts_, pts_rate_, static_cast<size_t>(packet.valid_length_bytes),
      payload, packet.header.buffer_lifetime_ordinal,
      packet.header.packet_index, this);

  if (revised_stream_type_) {
    decoder_packet->SetRevisedStreamType(std::move(revised_stream_type_));
  }

  stage()->PutOutputPacket(std::move(decoder_packet));
}

void FidlDecoder::OnOutputEndOfStream(uint64_t stream_lifetime_ordinal,
                                      bool error_detected_before) {
  if (error_detected_before) {
    FXL_LOG(WARNING) << "OnOutputEndOfStream: error_detected_before";
  }

  stage()->PutOutputPacket(Packet::CreateEndOfStream(next_pts_, pts_rate_));
}

void FidlDecoder::OnFreeInputPacket(
    fuchsia::mediacodec::CodecPacketHeader packet_header) {
  if (input_buffers_.FreeBuffer(packet_header.buffer_lifetime_ordinal,
                                packet_header.packet_index) &&
      !end_of_input_stream_) {
    stage()->RequestInputPacket();
  }
}

void FidlDecoder::RecycleOutputPacket(uint64_t buffer_lifetime_ordinal,
                                      uint32_t buffer_index) {
  if (!output_buffers_.has_current_set() ||
      buffer_lifetime_ordinal !=
          output_buffers_.current_set().lifetime_ordinal()) {
    output_buffers_.FreeBuffer(buffer_lifetime_ordinal, buffer_index);
    return;
  }

  FXL_DCHECK(buffer_lifetime_ordinal ==
             output_buffers_.current_set().lifetime_ordinal());
  output_buffers_.current_set().TransferBuffer(buffer_index, kCodec);

  fuchsia::mediacodec::CodecPacketHeader header;
  header.buffer_lifetime_ordinal = buffer_lifetime_ordinal;
  header.packet_index = buffer_index;
  outboard_decoder_->RecycleOutputPacket(std::move(header));
}

void FidlDecoder::HandlePossibleOutputStreamTypeChange(
    const StreamType& old_type, const StreamType& new_type) {
  // TODO(dalesat): Actually compare the types.
  revised_stream_type_ = new_type.Clone();
}

FidlDecoder::DecoderPacket::~DecoderPacket() {
  owner_->RecycleOutputPacket(buffer_lifetime_ordinal_, buffer_index_);
}

}  // namespace media_player
