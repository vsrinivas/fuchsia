// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_PEEKER_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_PEEKER_H_

#include <memory>

#include "in_stream.h"

// This wrapper of an InStream adds the ability to peek into the stream.
//
// As with InStream, this class has blocking methods, and completion of those
// methods relies on the FIDL thread being a separate thread.
class InStreamPeeker : public InStream {
 public:
  // in_stream_to_wrap - the underlying source of data, typically not capable
  //     of peeking, to wrap such that peeking is possible.
  // max_peek_bytes - the maximum peek distance in bytes.  Some usages will need
  //     a peek distance that's as large as an AU, such as when searching for
  //     pattern-based start codes.  Others may not need much peek distance at
  //     all, such as when headers at the start of each AU indicate the length
  //     of the AU.
  //
  // This InStreamPeeker takes ownership of in_stream_to_wrap and does not
  // provide any direct access to in_stream_to_wrap, since the read-ahead
  // performed by this instance would only confuse any direct use of
  // in_stream_to_wrap.
  //
  // The in_stream_to_wrap is only called during ReadBytes() or PeekBytes(),
  // using the same thread as those calls are made on.
  //
  // The first three parameters to this constructor are for consistency in
  // threading across all InStream types.  We want the InStream base class to
  // be able to assert that methods are being called on the correct thread, etc.
  InStreamPeeker(async::Loop* fidl_loop, thrd_t fidl_thread,
                 sys::ComponentContext* component_context,
                 std::unique_ptr<InStream> in_stream_to_wrap, uint32_t max_peek_bytes);
  ~InStreamPeeker();

  uint32_t max_peek_bytes();

  // Unlike ReadBytes, PeekBytes() does not advance cursor_position().
  //
  // Unlike ReadBytes, PeekBytes() provides a memory address at which the caller
  // can observe peeked data.  The *peek_buffer_out pointer remains valid to
  // read up to *peek_bytes_out from until the next call to any non-const method
  // or destructor of this instance.
  //
  // If the timeout is exceeded, ZX_ERR_TIMED_OUT is returned, and
  // *peek_buffer_out is set to nullptr.
  //
  // The *peek_bytes_out may be less than desired_bytes_to_peek only if the end
  // of input data is reached and has offset < cursor_position() +
  // desired_bytes_to_peek.
  zx_status_t PeekBytes(uint32_t desired_bytes_to_peek, uint32_t* peek_bytes_out,
                        uint8_t** peek_buffer_out,
                        zx::time just_fail_deadline = zx::time::infinite());

  // Discard previously-peeked and not-yet-read/not-yet-tossed bytes.
  //
  // This will assert in debug that bytes_to_toss is consistent with having
  // previously been peeked, but /may/ not catch all cases where this is called
  // incorrectly without a previous peek of all these bytes.
  //
  // The caller must only call this for bytes which were previously peeked.
  void TossPeekedBytes(uint32_t bytes_to_toss);

 private:
  // This InStream sub-class guarantees that reads which only read previously
  // peeked bytes will be satisfied in their entirety.  Reads beyond previously
  // peeked bytes can be short like usual.
  zx_status_t ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                uint8_t* buffer_out, zx::time just_fail_deadline) override;

  zx_status_t ReadMoreIfPossible(uint32_t bytes_to_read_if_possible, zx::time just_fail_deadline);

  void PropagateEosKnown();

  // Set at construction time.
  const std::unique_ptr<InStream> in_stream_;
  const uint32_t max_peek_bytes_ = 0;

  // max_peek_bytes_ rounded up to next PAGE_SIZE
  uint64_t vmo_size_bytes_;

  uint32_t write_offset_ = 0;
  uint32_t read_offset_ = 0;
  uint32_t valid_bytes_ = 0;

  uint8_t* ring_base_ = nullptr;
  zx::vmo ring_vmo_;

  // We need to ensure that reads via one mapping are done before writes via the
  // other mapping, and that writes via one mapping are done before reads via
  // the other mapping.  In both places, we care about both release and acquire,
  // so we read-modify-write this atomic using memory_order_acq_rel both before
  // and after writing to the ring.
  //
  // The actual writes and reads are all occuring on a single ordering domain
  // (such as a single thread, or guaranteed sequential method calls), it's just
  // that the reads and writes via different mappings is the sort of aliasing
  // that compiler optimizations like to pretend can't exist.
  //
  // To understand how this helps, it may help to consider the analogous case
  // where writes to a buffer are performed by a different thread, and the
  // release/acquire separating the reads from writes is a lock release by one
  // thread and lock acquire by another thread.
  std::atomic<uint32_t> ring_memory_fence_;

  // Double-map a VMO that's at least max_peek_bytes in size, with the two
  // mappings adjacent in VA space.  This treats the VMO as a ring buffer, with
  // the adjacent double mapping permitting contiguous VA access to any portion
  // of the ring buffer including portions that would normally need to be split
  // into two pieces due to crossing the end of the buffer and continuing at the
  // start of the buffer.
  //
  // The ring_vmar_ is 2x the size of ring_vmo_, to make room to double-map the
  // ring_vmo_.
  zx::vmar ring_vmar_;
};

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_PEEKER_H_
