// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_BOUNDED_WRITER_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_BOUNDED_WRITER_H_

#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// Created fixed length writer over another Writer, where any write exceeding its length is treated
// as an error.
class BoundedWriter final : public Writer {
 public:
  BoundedWriter(std::unique_ptr<Writer> writer, uint64_t offset, uint64_t length)
      : offset_(offset), length_(length), writer_(std::move(writer)) {}

  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    if (offset + buffer.size() > length_) {
      return fpromise::error(
          "BoundedWriter::Write out of bounds. offset: " + std::to_string(offset) + " byte_cout: " +
          std::to_string(buffer.size()) + " min_offset: " + std::to_string(offset_) +
          " max_offset: " + std::to_string(offset_ + length_ - 1) + ".");
    }
    return writer_->Write(offset_ + offset, buffer);
  }

 private:
  uint64_t offset_ = 0;
  uint64_t length_ = 0;
  std::unique_ptr<Writer> writer_;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_BOUNDED_WRITER_H_
