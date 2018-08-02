// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_STREAM_BUFFER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_STREAM_BUFFER_H_
#include <ddk/io-buffer.h>

// The stream buffer is a fifo between parser and decoder.
class StreamBuffer {
 public:
  ~StreamBuffer() { io_buffer_release(&buffer_); }

  io_buffer_t* buffer() { return &buffer_; }

 private:
  io_buffer_t buffer_ = {};
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_STREAM_BUFFER_H_
