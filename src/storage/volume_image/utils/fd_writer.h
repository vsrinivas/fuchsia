// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_WRITER_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_WRITER_H_

#include <lib/fit/result.h>

#include <string>
#include <string_view>

#include <fbl/span.h>
#include <fbl/unique_fd.h>

#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// Writer implementation that interacts reads from a file descriptor.
class FdWriter final : public Writer {
 public:
  // On success returns a |FdWriter| from a file descriptor pointing to |path|, and whose name is
  // |path|.
  static fit::result<FdWriter, std::string> Create(std::string_view path);

  explicit FdWriter(fbl::unique_fd fd) : FdWriter(std::move(fd), std::string_view()) {}
  FdWriter(fbl::unique_fd fd, std::string_view name) : fd_(std::move(fd)), name_(name) {}
  FdWriter(const FdWriter&) = delete;
  FdWriter(FdWriter&&) = default;
  FdWriter& operator=(const FdWriter&) = delete;
  FdWriter& operator=(FdWriter&&) = default;

  // On success data backing this writer is updated at [|offset|, |offset| +
  // |buffer.size()|] to |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> buffer) final;

  // Returns a unique identifier for this |FdWriter|.
  std::string_view name() const { return name_; }

 private:
  fbl::unique_fd fd_;

  // Stores a unique name for the resource represented by |fd_|, for properly reporting errors.
  std::string name_;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_WRITER_H_
