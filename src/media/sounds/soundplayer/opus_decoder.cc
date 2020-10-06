// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/sounds/soundplayer/opus_decoder.h"

#include <endian.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "third_party/opus/include/opus.h"

namespace soundplayer {
namespace {

// Creates a 64-bit value from 8 bytes. Used for Opus signature value.
static inline constexpr uint64_t make_eightcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e,
                                              uint8_t f, uint8_t g, uint8_t h) {
  return htole64((static_cast<uint64_t>(h) << 56) | (static_cast<uint64_t>(g) << 48) |
                 (static_cast<uint64_t>(f) << 40) | (static_cast<uint64_t>(e) << 32) |
                 (static_cast<uint64_t>(d) << 24) | (static_cast<uint64_t>(c) << 16) |
                 (static_cast<uint64_t>(b) << 8) | static_cast<uint64_t>(a));
}

static constexpr uint64_t kIdHeaderSignature = make_eightcc('O', 'p', 'u', 's', 'H', 'e', 'a', 'd');
static constexpr uint64_t kCommentHeaderSignature =
    make_eightcc('O', 'p', 'u', 's', 'T', 'a', 'g', 's');
static constexpr uint8_t kSupportedVersion = 1;
static constexpr uint8_t kSupportedMappingFamily = 0;
static constexpr uint32_t kOutputFramesPerSecond = 48000;
static constexpr size_t kOutputBufferMaxFrameCount = 5760;  // 120ms at 48k

struct IdHeader {
  uint64_t signature_;  // 'OpusHead' (kIdHeaderSignature)
  uint8_t version_;
  uint8_t channel_count_;
  uint16_t preskip_;
  uint32_t input_sample_rate_;
  uint16_t output_gain_;
  uint8_t mapping_family_;
} __PACKED;

struct CommentHeader {
  uint64_t signature_;  // 'OpusTags' (kCommentHeaderSignature)
  // Variable stuff follows. Not used.
} __PACKED;

}  // namespace

// static
bool OpusDecoder::CheckHeaderPacket(const uint8_t* data, size_t size) {
  if (size < sizeof(IdHeader)) {
    // Header too small.
    return false;
  }

  auto& header = *reinterpret_cast<const IdHeader*>(data);
  if (header.signature_ != kIdHeaderSignature) {
    // Signature not found.
    return false;
  }

  if (header.version_ != kSupportedVersion) {
    // Unsupported version.
    return false;
  }

  if (header.channel_count_ != 1 && header.channel_count_ != 2) {
    // Unsupported channel count.
    return false;
  }

  if (header.mapping_family_ != kSupportedMappingFamily) {
    // Unsupported mapping family.
    return false;
  }

  return true;
}

OpusDecoder::OpusDecoder() {}

OpusDecoder::~OpusDecoder() {}

bool OpusDecoder::ProcessPacket(const uint8_t* data, size_t size, bool first, bool last) {
  if (first) {
    second_packet_processed_ = false;
    total_frame_count_ = 0;
    output_buffers_.clear();
    return ProcessIdHeader(data, size);
  }

  if (!second_packet_processed_) {
    second_packet_processed_ = true;
    return ProcessCommentHeader(data, size);
  }

  FX_DCHECK(decoder_);

  auto output_buffer = std::make_unique<int16_t[]>(kOutputBufferMaxFrameCount * channels_);

  int decoded_frame_count_or_error =
      opus_decode(decoder_.get(), data, size, output_buffer.get(), kOutputBufferMaxFrameCount, 0);
  if (decoded_frame_count_or_error < 0) {
    // Decode failed.
    FX_LOGS(WARNING) << "opus_decode failed, error " << decoded_frame_count_or_error;
    return false;
  }

  HandleOutputBuffer(std::move(output_buffer), static_cast<uint32_t>(decoded_frame_count_or_error));

  if (last) {
    return HandleEndOfStream();
  }

  return true;
}

bool OpusDecoder::ProcessIdHeader(const uint8_t* data, size_t size) {
  // |CheckHeaderPacket| approved the header previously, so we can just DCHECK what was checked
  // there.
  FX_DCHECK(size >= sizeof(IdHeader));

  auto& header = *reinterpret_cast<const IdHeader*>(data);
  FX_DCHECK(header.signature_ == kIdHeaderSignature);
  FX_DCHECK(header.version_ == kSupportedVersion);
  FX_DCHECK(header.channel_count_ == 1 || header.channel_count_ == 2);
  FX_DCHECK(header.mapping_family_ == kSupportedMappingFamily);

  channels_ = header.channel_count_;
  preskip_ = header.preskip_;
  input_frames_per_second_ = header.input_sample_rate_;

  int error;
  decoder_ = std::unique_ptr<::OpusDecoder, DecoderDeleter>(
      opus_decoder_create(kOutputFramesPerSecond, channels_, &error));
  if (!decoder_) {
    FX_LOGS(ERROR) << "opus_decoder_create failed, error " << error;
    return false;
  }

  return true;
}

bool OpusDecoder::ProcessCommentHeader(const uint8_t* data, size_t size) {
  if (size < sizeof(CommentHeader)) {
    // Header too small.
    return false;
  }

  auto& header = *reinterpret_cast<const CommentHeader*>(data);
  if (header.signature_ != kCommentHeaderSignature) {
    // Signature not found.
    return false;
  }

  // If metadata from the comment header is needed, parse it here.

  return true;
}

void OpusDecoder::HandleOutputBuffer(std::unique_ptr<int16_t[]> buffer, uint32_t frame_count) {
  output_buffers_.emplace_back(std::move(buffer), frame_count);
  total_frame_count_ += frame_count;
}

bool OpusDecoder::HandleEndOfStream() {
  size_t frame_size = sizeof(int16_t) * channels_;
  size_t vmo_size = (total_frame_count_ - preskip_) * frame_size;

  zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo_);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "zx::vmo::create failed";
    return false;
  }

  uint16_t preskip_remaining = preskip_;
  uint64_t offset = 0;
  for (auto& output_buffer : output_buffers_) {
    if (preskip_remaining >= output_buffer.frame_count_) {
      preskip_remaining -= output_buffer.frame_count_;
      continue;
    }

    size_t size = (output_buffer.frame_count_ - preskip_remaining) * frame_size;
    status = vmo_.write(output_buffer.samples_.get() + preskip_remaining * channels_, offset, size);
    if (status != ZX_OK) {
      FX_PLOGS(WARNING, status) << "zx::vmo::write failed";
      return false;
    }

    offset += size;
    preskip_remaining = 0;
  }

  output_buffers_.clear();
  return true;
}

Sound OpusDecoder::TakeSound() {
  if (!vmo_) {
    return Sound();
  }

  return Sound(
      std::move(vmo_), (total_frame_count_ - preskip_) * sizeof(int16_t) * channels_,
      fuchsia::media::AudioStreamType{.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                                      .channels = channels_,
                                      .frames_per_second = kOutputFramesPerSecond});
}

}  // namespace soundplayer
