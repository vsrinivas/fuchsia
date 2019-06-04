// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_SBC_CODEC_ADAPTER_SBC_ENCODER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_SBC_CODEC_ADAPTER_SBC_ENCODER_H_

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/logging.h>

#include "codec_adapter_sw.h"
#include "timestamp_extrapolator.h"

// This must be included after fuchsia.media FIDL because it defines macros
// that conflict with the SBC types.
#include <sbc_encoder.h>

class CodecAdapterSbcEncoder
    : public CodecAdapterSW<fit::deferred_action<fit::closure>> {
 public:
  CodecAdapterSbcEncoder(std::mutex& lock,
                         CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterSbcEncoder();

  fuchsia::sysmem::BufferCollectionConstraints
  CoreCodecGetBufferCollectionConstraints(
      CodecPort port,
      const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings)
      override;

  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info)
      override;

 protected:
  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  void ProcessInputLoop() override;

  void CleanUpAfterStream() override;

  std::pair<fuchsia::media::FormatDetails, size_t> OutputFormatDetails()
      override;

 private:
  // A block of PCM data for holding leftovers from unaligned input packets from
  // clients. Data always starts at byte 0.
  struct ScratchBlock {
    uint8_t buffer[SBC_MAX_PCM_BUFFER_SIZE];
    size_t len = 0;
  };

  struct Context {
    fuchsia::media::SbcEncoderSettings settings;
    fuchsia::media::PcmFormat input_format;
    SBC_ENC_PARAMS params;
    // The output packet we are currently encoding into.
    CodecPacket* output_packet = nullptr;
    // The input packet we are currently encoding.
    CodecPacket* input_packet = nullptr;
    // Number of bytes we've already read from the input packet.
    size_t input_offset = 0;
    // Space for buffering PCM frames when the client sends frames that don't
    // line up with SBC batch sizes.
    ScratchBlock scratch_block;
    // Next byte in the uncompressed stream we will output.
    size_t input_stream_index = 0;
    TimestampExtrapolator timestamp_extrapolator;

    size_t sbc_frame_length() const {
      const size_t part = 4 + params.s16NumOfSubBands * channel_count() / 2;
      switch (settings.channel_mode) {
        case fuchsia::media::SbcChannelMode::MONO:
        case fuchsia::media::SbcChannelMode::DUAL:
          return part + static_cast<size_t>(
                            ceil(static_cast<double>(params.s16NumOfBlocks) *
                                 static_cast<double>(channel_count()) *
                                 static_cast<double>(params.s16BitPool) / 8.0));
        case fuchsia::media::SbcChannelMode::JOINT_STEREO:
          return part + static_cast<size_t>(
                            ceil((static_cast<double>(params.s16NumOfSubBands) +
                                  static_cast<double>(params.s16NumOfBlocks) *
                                      static_cast<double>(params.s16BitPool)) /
                                 8.0));
        case fuchsia::media::SbcChannelMode::STEREO:
          return part + static_cast<size_t>(
                            ceil(static_cast<double>(params.s16NumOfBlocks) *
                                 static_cast<double>(params.s16BitPool) / 8.0));
        default:
          FXL_LOG(FATAL) << "Channel mode enum became invalid value: "
                         << static_cast<int>(settings.channel_mode);
      }
    }

    size_t pcm_frames_per_sbc_frame() const {
      return params.s16NumOfBlocks * params.s16NumOfSubBands;
    }

    size_t pcm_frame_size() const {
      return input_format.bits_per_sample / 8 * channel_count();
    }

    size_t pcm_batch_size() const {
      return pcm_frame_size() * pcm_frames_per_sbc_frame();
    }

    size_t channel_count() const { return input_format.channel_map.size(); }
  };

  enum InputLoopStatus {
    kOk = 0,
    kShouldTerminate = 1,
  };

  // Attempts to create a context from format details. Reports failures through
  // `events_`.
  InputLoopStatus CreateContext(
      const fuchsia::media::FormatDetails& format_details);

  // Attempts to encode input packet. Reports failures through `events_`.
  InputLoopStatus EncodeInput(CodecPacket* input_packet);

  void SendOutputPacket(CodecPacket* output_packet);

  // Saves the leftovers of the input packet to our scratch space.
  void SaveLeftovers();

  // Sets the input packet for the encode state.
  void SetInputPacket(CodecPacket* input_packet);

  // Ensures we have an output packet to encode into if there are any input
  // bytes at all, whether there is enough to encode or not.
  InputLoopStatus EnsureOutputPacketIsSetIfAnyInputBytesRemain();

  // Advances the encoder context to the next input block, returning a pointer
  // to the block so it can fed to the encoder. The returned pointer may become
  // invalid next time SetInputPacket() is called.
  std::pair<uint8_t*, InputLoopStatus>
  EnsureOutputPacketIsFineAndGetNextInputBlock();

  std::optional<Context> context_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_SBC_CODEC_ADAPTER_SBC_ENCODER_H_
