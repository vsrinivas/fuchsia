// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_RESULT_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_RESULT_H_

#include <lib/fit/result.h>

#include <string>
#include <vector>

#include <fbl/span.h>
#include <lz4/lz4frame.h>

#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/decompressor.h"

namespace storage::volume_image {

// Wrapper on top of LZ4* function return codes.
class Lz4Result {
 public:
  // Implicit conversion from LZ4F_error_code_t.
  Lz4Result(LZ4F_errorCode_t code) : code_(code) {}

  // Returns true if the underlying |code_| is not an error.
  bool is_ok() const { return !is_error(); }

  // Returns true if the underlying |code_| is an error.
  bool is_error() const { return LZ4F_isError(code_); }

  // Returns a view into the error name of the underlying |code_|.
  std::string_view error() const {
    assert(is_error());
    return std::string_view(LZ4F_getErrorName(code_));
  }

  // Returns the byte count, when overriden return value happens. This usually means that
  // a function either returns a negative value or a number of bytes.
  size_t byte_count() const {
    assert(is_ok() && code_ >= 0);
    return static_cast<size_t>(code_);
  }

 private:
  LZ4F_errorCode_t code_ = -1;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_RESULT_H_
