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

  void set_data_size(uint32_t data_size) { data_size_ = data_size; }
  uint32_t data_size() const { return data_size_; }

  void set_padding_size(uint32_t padding_size) { padding_size_ = padding_size; }
  uint32_t padding_size() const { return padding_size_; }

 private:
  io_buffer_t buffer_ = {};
  // Amount of data written to this buffer, in bytes.
  uint32_t data_size_ = 0;
  uint32_t padding_size_ = 0;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_STREAM_BUFFER_H_
