// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_BYTES_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_BYTES_H_

#include <memory>
#include <vector>

#include "lib/fxl/logging.h"

namespace media_player {

class Bytes {
 public:
  static std::unique_ptr<Bytes> Create(size_t size) {
    return std::unique_ptr<Bytes>(new Bytes(size));
  }

  static std::unique_ptr<Bytes> Create(const uint8_t* data, size_t size) {
    std::unique_ptr<Bytes> result = Create(size);
    if (size != 0) {
      FXL_DCHECK(result->data());
      FXL_DCHECK(data);
      std::memcpy(result->data(), data, size);
    }
    return result;
  }

  ~Bytes();

  std::unique_ptr<Bytes> Clone() const;

  uint8_t* data() { return storage_.data(); }

  const uint8_t* data() const { return storage_.data(); }

  size_t size() const { return storage_.size(); }

 private:
  explicit Bytes(size_t size);

  std::vector<uint8_t> storage_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_BYTES_H_
