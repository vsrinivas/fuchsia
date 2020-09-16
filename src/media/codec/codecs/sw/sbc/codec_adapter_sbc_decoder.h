// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_SW_SBC_CODEC_ADAPTER_SBC_DECODER_H_
#define SRC_MEDIA_CODEC_CODECS_SW_SBC_CODEC_ADAPTER_SBC_DECODER_H_

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <oi_codec_sbc.h>
#include <oi_status.h>

#include "chunk_input_stream.h"
#include "codec_adapter_sw.h"
#include "fuchsia/media/cpp/fidl.h"
#include "timestamp_extrapolator.h"

constexpr uint8_t kSbcSamplingFrequency16000Hz = 0b1000;
constexpr uint8_t kSbcSamplingFrequency32000Hz = 0b0100;
constexpr uint8_t kSbcSamplingFrequency44100Hz = 0b0010;
constexpr uint8_t kSbcSamplingFrequency48000Hz = 0b0001;

constexpr uint8_t kSbcChannelModeMono = 0b1000;
constexpr uint8_t kSbcChannelModeDualChannel = 0b0100;
constexpr uint8_t kSbcChannelModeStereo = 0b0010;
constexpr uint8_t kSbcChannelModeJointStereo = 0b0001;

typedef struct {
  uint8_t sampling_frequency : 4;
  uint8_t channel_mode : 4;
  uint8_t block_length : 4;
  uint8_t subbands : 2;
  uint8_t allocation_method : 2;
  uint8_t min_bitpool_value;
  uint8_t max_bitpool_value;
} __attribute__((packed)) SbcCodecInfo;

class CodecAdapterSbcDecoder : public CodecAdapterSW<fit::deferred_action<fit::closure>> {
 public:
  CodecAdapterSbcDecoder(std::mutex& lock, CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterSbcDecoder();

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
    OI_CODEC_SBC_DECODER_CONTEXT context;
    uint32_t context_data[CODEC_DATA_WORDS(SBC_MAX_CHANNELS, SBC_CODEC_FAST_FILTER_BUFFERS)];

    fuchsia::media::PcmFormat output_format;

    uint32_t max_pcm_chunk_size() {
      return (output_format.bits_per_sample / 8) * SBC_MAX_SAMPLES_PER_FRAME * SBC_MAX_CHANNELS;
    }
  };

  enum InputLoopStatus {
    kOk = 0,
    kShouldTerminate = 1,
  };

  /// Attempts to create a context from format details. Reports failures through
  /// `events_`.
  ///
  /// To configure the decoder output format, oob_bytes must be set in the format specified in the
  /// Bluetooth A2DP spec, as follows:
  ///
  /// SBC Codec Specific Information Elements (A2DP Sec. 4.3.2).
  /// Packet structure:
  ///     Octet0: Sampling Frequency (b4-7), Channel Mode (b0-3)
  ///     Octet1: Block Length (b4-7), Subbands (b2-3), Allocation Method (b0-1)
  ///     Octet2: Minimum Bitpool Value [2,250]
  ///     Octet3: Maximum Bitpool Value [2,250]
  InputLoopStatus CreateContext(const fuchsia::media::FormatDetails& format_details);

  // Attempts to decode input packet. Reports failures through `events_`.
  InputLoopStatus DecodeInput(CodecPacket* input_packet);

  // Extract PCM format from SBC codec info bytes
  static fuchsia::media::PcmFormat DecodeCodecInfo(const std::vector<uint8_t>& oob_bytes);

  // Attempts to setup an output packet and return a pointer into `output_buffer_` at
  // `output_offset_`. Caller should ensure output_offset_ does not exceed output buffer size
  uint8_t* CurrentOutputBlock();

  // Increment `output_offset_` and send output packet if full, clearing output state
  void QueueAndSend(size_t bytes_read);

  // If any data is queued, send it and clear output packet/buffer/offset.
  void SendQueuedOutput();

  std::optional<Context> context_;
  // The output packet we are currently decoding into.
  CodecPacket* output_packet_ = nullptr;
  // The output buffer we are currently decoding into.
  const CodecBuffer* output_buffer_ = nullptr;
  // Offset into the output buffer we're decoding into.
  size_t output_offset_ = 0;
};

#endif  // SRC_MEDIA_CODEC_CODECS_SW_SBC_CODEC_ADAPTER_SBC_DECODER_H_
