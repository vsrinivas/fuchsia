// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_TEST_RAW_AUDIO_H_
#define GARNET_BIN_MEDIA_CODECS_TEST_RAW_AUDIO_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <optional>
#include <vector>

class RawAudio {
 public:
  static RawAudio FromAUFile(const std::string& filename);

  struct CodecInput {
    const std::vector<uint8_t>& data;
    const std::vector<size_t> payload_offsets;
    fuchsia::media::FormatDetails format;
  };

  CodecInput BuildCodecInput(size_t max_frames_per_packet) const;

 private:
  struct SignedLinear16BitLayout {
    uint32_t frequency;
    uint32_t channels;
  };

  RawAudio(SignedLinear16BitLayout layout, std::vector<uint8_t> data);

  size_t frame_size() const;
  size_t frame_count() const;

  SignedLinear16BitLayout layout_;
  std::vector<uint8_t> data_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_TEST_RAW_AUDIO_H_
