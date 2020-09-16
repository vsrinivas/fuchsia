// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_SW_SBC_CODEC_ADAPTER_SBC_ENCODER_H_
#define SRC_MEDIA_CODEC_CODECS_SW_SBC_CODEC_ADAPTER_SBC_ENCODER_H_

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include "chunk_input_stream.h"
#include "codec_adapter_sw.h"
#include "timestamp_extrapolator.h"

// This must be included after fuchsia.media FIDL because it defines macros
// that conflict with the SBC types.
#include <sbc_encoder.h>

class CodecAdapterSbcEncoder : public CodecAdapterSW<fit::deferred_action<fit::closure>> {
 public:
  CodecAdapterSbcEncoder(std::mutex& lock, CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterSbcEncoder();

  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override;

  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override;

  void CoreCodecStopStream() override;

 protected:
  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  void ProcessInputLoop() override;

  void CleanUpAfterStream() override;

  std::pair<fuchsia::media::FormatDetails, size_t> OutputFormatDetails() override;

 private:
  struct Context {
    fuchsia::media::SbcEncoderSettings settings;
    fuchsia::media::PcmFormat input_format;
    SBC_ENC_PARAMS params;

    size_t sbc_frame_length() const {
      const size_t part = 4 + params.s16NumOfSubBands * channel_count() / 2;
      switch (settings.channel_mode) {
        case fuchsia::media::SbcChannelMode::MONO:
        case fuchsia::media::SbcChannelMode::DUAL:
          return part + static_cast<size_t>(ceil(static_cast<double>(params.s16NumOfBlocks) *
                                                 static_cast<double>(channel_count()) *
                                                 static_cast<double>(params.s16BitPool) / 8.0));
        case fuchsia::media::SbcChannelMode::JOINT_STEREO:
          return part + static_cast<size_t>(ceil((static_cast<double>(params.s16NumOfSubBands) +
                                                  static_cast<double>(params.s16NumOfBlocks) *
                                                      static_cast<double>(params.s16BitPool)) /
                                                 8.0));
        case fuchsia::media::SbcChannelMode::STEREO:
          return part + static_cast<size_t>(ceil(static_cast<double>(params.s16NumOfBlocks) *
                                                 static_cast<double>(params.s16BitPool) / 8.0));
        default:
          FX_LOGS(FATAL) << "Channel mode enum became invalid value: "
                         << static_cast<int>(settings.channel_mode);
      }
    }

    size_t pcm_frames_per_sbc_frame() const {
      return params.s16NumOfBlocks * params.s16NumOfSubBands;
    }

    size_t pcm_frame_size() const { return input_format.bits_per_sample / 8 * channel_count(); }

    size_t pcm_batch_size() const { return pcm_frame_size() * pcm_frames_per_sbc_frame(); }

    size_t channel_count() const { return input_format.channel_map.size(); }
  };

  enum InputLoopStatus {
    kOk = 0,
    kShouldTerminate = 1,
  };

  // Attempts to create a context from format details. Reports failures through
  // `events_`.
  InputLoopStatus CreateContext(const fuchsia::media::FormatDetails& format_details);

  // Attempts to encode input packet. Reports failures through `events_`.
  InputLoopStatus EncodeInput(CodecPacket* input_packet);

  void SendOutputPacket(CodecPacket* output_packet);

  uint8_t* NextOutputBlock();

  std::optional<Context> context_;
  // The output packet we are currently encoding into.
  CodecPacket* output_packet_ = nullptr;
  // The output buffer we are currently encoding into.
  const CodecBuffer* output_buffer_ = nullptr;
  // Offset into the output buffer we're encoding into.
  size_t output_offset_ = 0;
  std::optional<ChunkInputStream> chunk_input_stream_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_SW_SBC_CODEC_ADAPTER_SBC_ENCODER_H_
