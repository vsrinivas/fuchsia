// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_BUFFER_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_BUFFER_H_

#include <memory>

#include "in_stream.h"

// This wrapper of an InStream buffers the entire InStream on first read through
// and supports ResetToStart() even if the wrapped InStream doesn't.
//
// As with InStream, this class has blocking methods, and completion of those
// methods relies on the FIDL thread being a separate thread.
class InStreamBuffer : public InStream {
 public:
  // in_stream_to_wrap - the underlying source of data, typically not capable
  //     of ResetToStart(), to wrap such that ResetToStart() is possible and
  //     fast.
  //
  // This InStreamBuffer takes ownership of in_stream_to_wrap and does not
  // provide any direct access to in_stream_to_wrap, since the ResetToStart()
  // performed by this instance would only confuse any direct use of
  // in_stream_to_wrap.
  //
  // The in_stream_to_wrap is only called during ReadBytes(), using the same
  // thread as those calls are made on.
  //
  // The first three parameters to this constructor are for consistency in
  // threading across all InStream types.  We want the InStream base class to
  // be able to assert that methods are being called on the correct thread, etc.
  InStreamBuffer(async::Loop* fidl_loop, thrd_t fidl_thread,
                 sys::ComponentContext* component_context,
                 std::unique_ptr<InStream> in_stream_to_wrap, uint64_t max_buffer_size);
  ~InStreamBuffer() override;

 protected:
  zx_status_t ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                uint8_t* buffer_out, zx::time just_fail_deadline) override;

  zx_status_t ResetToStartInternal(zx::time just_fail_deadline) override;

  zx_status_t ReadMoreIfPossible(uint32_t bytes_to_read_if_possible, zx::time just_fail_deadline);

  void PropagateEosKnown();

  // Set at construction time.
  const std::unique_ptr<InStream> in_stream_;
  const uint64_t max_buffer_size_ = 0;

  uint64_t valid_bytes_ = 0;

  std::vector<uint8_t> data_;
};

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_BUFFER_H_
