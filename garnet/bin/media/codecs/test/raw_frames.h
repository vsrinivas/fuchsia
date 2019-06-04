// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_TEST_RAW_FRAMES_H_
#define GARNET_BIN_MEDIA_CODECS_TEST_RAW_FRAMES_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <optional>
#include <string>

// RawFrames loads a test file with raw uncompressed frames into RAM in YV12
// format and prepares them for sending to a decoder or image pipe for testing.
class RawFrames {
 public:
  struct Layout {
    // Width of the source video.
    size_t width;
    // Height of the source video.
    size_t height;
    // Row stride in bytes.
    size_t stride;
    // Alignment for the start of each frame.
    size_t frame_alignment;
  };

  struct Image {
    fuchsia::media::VideoUncompressedFormat format;
    zx::vmo vmo;
    size_t vmo_offset;
    size_t image_size;
    uint8_t* image_start;
  };

  static std::optional<RawFrames> FromI420File(std::string file, Layout layout);

  std::optional<Image> Frame(size_t frame_index);

  size_t frame_count() const;

 private:
  RawFrames(Layout layout, zx::vmo frames, fzl::VmoMapper mapper,
            size_t frame_stored_size, size_t frame_count);

  Layout layout_;
  zx::vmo frames_;
  fzl::VmoMapper mapper_;
  size_t frame_stored_size_;
  size_t frame_count_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_TEST_RAW_FRAMES_H_
