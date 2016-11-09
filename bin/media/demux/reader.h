// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits>
#include <functional>
#include <memory>

#include "apps/media/src/framework/result.h"

namespace media {

// Abstract base class for objects that read raw data on behalf of demuxes.
class Reader {
 public:
  using DescribeCallback =
      std::function<void(Result result, size_t size, bool can_seek)>;
  using ReadAtCallback = std::function<void(Result result, size_t bytes_read)>;

  static constexpr size_t kUnknownSize = std::numeric_limits<size_t>::max();

  virtual ~Reader() {}

  // Returns a result, the file size and whether the reader supports seeking
  // via a callback. The returned size is kUnknownSize if the content size isn't
  // known.
  virtual void Describe(const DescribeCallback& callback) = 0;

  // Reads the specified number of bytes into the buffer from the specified
  // position and returns a result and the number of bytes read via the
  // callback.
  virtual void ReadAt(size_t position,
                      uint8_t* buffer,
                      size_t bytes_to_read,
                      const ReadAtCallback& callback) = 0;
};

}  // namespace media
