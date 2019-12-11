// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <fbl/mutex.h>

// Abstract base class which permits reading from a stream of input data.
//
// Sub-classes:
//  * InStreamFile can stream in from a file.
//  * InStreamHttp can stream in using http.
//
// Re. threading, this class is meant to be called from a single ordering
// domain / thread that isn't the fidl_thread.  None of the public methods are
// safe to call from the fidl_thread; also not the destructor.
//
// All methods including the destructor may rely on fidl_thread to make
// progress.
//
// Calls to ReadBytes() will block until there's at least 1 byte or the read
// times out.
//
// It is only safe to delete the instance from a thread that is not the
// fidl_thread.
//
// TODO(dustingreen): Probably we could combine this with
// src/media/playback/mediaplayer/demux/reader.h, assuming it's fine to consume
// that here.
class InStream {
 public:
  // All sub-classes must retain the property that it's safe to delete this
  // instance from any thread.
  //
  // Sub-classes may wish to use FencePostSerial() at the start of their
  // destructor to ensure that any lambdas previously posted with PostSerial()
  // are done before any sub-class member vars are destructed.
  virtual ~InStream();

  // The cursor_position() is the byte offset of the current location in the
  // input data.  This starts at 0.
  //
  // Every successful ReadBytes() advances cursor_position().
  //
  // This method is not meant to be overridden by sub-classes.
  uint64_t cursor_position() const;

  // Once this starts returning true it'll continue returning true.  At the
  // latest, this will start returning true when ReadBytesComplete() reads less
  // than the requested amount.
  bool eos_position_known();
  // Requires eos_position_known().
  uint64_t eos_position();

  // Returns ZX_OK with *bytes_read_out == 0 if the end of input data has been
  // reached.
  //
  // If the end of input data has not yet been reached, this blocks until at
  // least 1 bytes of input data is available, and then returns ZX_OK indicating
  // at least 1 byte was read.  The caller must not expect that the # of bytes
  // actaully read is necessarily max_bytes_to_read.  Especially as the end of
  // input data is reached, the *bytes_read_out will sometimes be less than
  // max_bytes_to_read.
  //
  // When deadline is non-infinite, and the deadline is reached, timeout will
  // occur before 1 byte is available and ZX_ERR_TIMED_OUT is returned.
  //
  // buffer_out must be at least max_bytes_to_read in length.
  //
  // The cursor_position is advanced by *bytes_read_out.
  //
  // Sub-classes should override ReadBytesInternal, not ReadBytes.
  zx_status_t ReadBytesShort(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                             uint8_t* buffer_out,
                             zx::time just_fail_deadline = zx::time::infinite());

  zx_status_t ReadBytesComplete(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                uint8_t* buffer_out,
                                zx::time just_fail_deadline = zx::time::infinite());

 protected:
  InStream(async::Loop* fidl_loop, thrd_t fidl_thread, sys::ComponentContext* component_context);

  void PostToFidlSerial(fit::closure to_run);
  void FencePostToFidlSerial();

  // Sub-classes override ReadBytesInternal() to actually read data.  The
  // sub-class doesn't need to update cursor_position_ since ReadBytes() handles
  // that.
  virtual zx_status_t ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                        uint8_t* buffer_out, zx::time just_fail_deadline) = 0;

  zx_status_t ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                uint8_t* buffer_out) {
    return ReadBytesInternal(max_bytes_to_read, bytes_read_out, buffer_out, zx::time::infinite());
  }

  async::Loop* const fidl_loop_ = nullptr;
  async_dispatcher_t* const fidl_dispatcher_ = nullptr;
  const thrd_t fidl_thread_{};
  sys::ComponentContext* const component_context_;

  // TODO(liyl): Need to change to std::mutex.
  fbl::Mutex lock_;

  uint64_t cursor_position_ = 0;
  bool failure_seen_ = false;
  bool eos_position_known_ = false;
  uint64_t eos_position_ = 0;
};

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_H_
