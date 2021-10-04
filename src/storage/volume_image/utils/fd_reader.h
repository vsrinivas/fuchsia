// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_READER_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_READER_H_

#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>

#include <string>
#include <string_view>

#include <fbl/unique_fd.h>

#include "src/storage/volume_image/utils/reader.h"

namespace storage::volume_image {

// Reader implementation that reads from a file descriptor with a fixed number of bytes.
class FdReader final : public Reader {
 public:
  // On success returns a |FdReader| from a file descriptor pointing to |path|, and whose name is
  // |path|.
  static fpromise::result<FdReader, std::string> Create(std::string_view path);

  explicit FdReader(fbl::unique_fd fd) : FdReader(std::move(fd), std::string_view()) {}
  FdReader(fbl::unique_fd fd, std::string_view name)
      : fd_(std::move(fd)), name_(name), length_(std::numeric_limits<uint64_t>::max()) {}
  FdReader(fbl::unique_fd fd, std::string_view name, uint64_t length)
      : fd_(std::move(fd)), name_(name), length_(length) {}
  FdReader(const FdReader&) = delete;
  FdReader(FdReader&&) = default;
  FdReader& operator=(const FdReader&) = delete;
  FdReader& operator=(FdReader&&) = default;

  // Returns the number of bytes readable from this reader.
  uint64_t length() const override { return length_; }

  // On success data at [|offset|, |offset| + |buffer.size()|] are read into
  // |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final;

  // Returns a unique identifier for this |FdReader|.
  std::string_view name() const { return name_; }

 private:
  fbl::unique_fd fd_;

  // Stores a unique name for the resource represented by |fd_|, for properly reporting errors.
  std::string name_;
  uint64_t length_ = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_READER_H_
