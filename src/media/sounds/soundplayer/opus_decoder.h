// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_SOUNDS_SOUNDPLAYER_OPUS_DECODER_H_
#define SRC_MEDIA_SOUNDS_SOUNDPLAYER_OPUS_DECODER_H_

#include <inttypes.h>
#include <lib/zx/vmo.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "src/media/sounds/soundplayer/sound.h"
#include "third_party/opus/include/opus.h"

namespace soundplayer {

class OpusDecoder {
 public:
  // Checks an initial stream packet to see if the stream is in Opus format.
  static bool CheckHeaderPacket(const uint8_t* data, size_t size);

  OpusDecoder();

  ~OpusDecoder();

  // Processes a packet. |first| indicates whether the packet is the first in the stream. |last|
  // indicates whether the packet is the last in the stream. Returns false on failure.
  bool ProcessPacket(const uint8_t* data, size_t size, bool first, bool last);

  // Takes the results. If a successful decode was not completed, all fields in the |Sound|
  // structure will be zero/null.
  Sound TakeSound();

 private:
  struct DecoderDeleter {
    void operator()(::OpusDecoder* decoder) const { opus_decoder_destroy(decoder); }
  };

  struct OutputBuffer {
    OutputBuffer(std::unique_ptr<int16_t[]> samples, uint32_t frame_count)
        : samples_(std::move(samples)), frame_count_(frame_count) {}

    std::unique_ptr<int16_t[]> samples_;
    uint32_t frame_count_;
  };

  // Processes the first packet, which contains the ID header. Returns false on failure.
  bool ProcessIdHeader(const uint8_t* data, size_t size);

  // Processes the second packet, which contains the comment header. Returns false on failure.
  bool ProcessCommentHeader(const uint8_t* data, size_t size);

  // Handles a buffer containing decoded PCM audio.
  void HandleOutputBuffer(std::unique_ptr<int16_t[]> buffer, uint32_t frame_count);

  // Performs end-of-stream tasks. Returns false on failure.
  bool HandleEndOfStream();

  uint32_t channels_;
  uint16_t preskip_;
  uint32_t input_frames_per_second_;
  std::unique_ptr<::OpusDecoder, DecoderDeleter> decoder_;
  bool second_packet_processed_;
  uint32_t total_frame_count_;
  std::vector<OutputBuffer> output_buffers_;
  zx::vmo vmo_;
};

}  // namespace soundplayer

#endif  // SRC_MEDIA_SOUNDS_SOUNDPLAYER_OPUS_DECODER_H_
