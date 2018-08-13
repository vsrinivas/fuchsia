// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_DEMUX_READER_H_
#define GARNET_BIN_MEDIAPLAYER_DEMUX_READER_H_

#include <limits>
#include <memory>

#include <lib/fit/function.h>

#include "garnet/bin/mediaplayer/framework/result.h"

namespace media_player {

// Abstract base class for objects that read raw data on behalf of demuxes.
class Reader {
 public:
  using DescribeCallback =
      fit::function<void(Result result, size_t size, bool can_seek)>;
  using ReadAtCallback = fit::function<void(Result result, size_t bytes_read)>;

  static constexpr size_t kUnknownSize = std::numeric_limits<size_t>::max();

  virtual ~Reader() {}

  // Returns a result, the file size and whether the reader supports seeking
  // via a callback. The returned size is kUnknownSize if the content size isn't
  // known.
  virtual void Describe(DescribeCallback callback) = 0;

  // Reads the specified number of bytes into the buffer from the specified
  // position and returns a result and the number of bytes read via the
  // callback.
  virtual void ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
                      ReadAtCallback callback) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_DEMUX_READER_H_
