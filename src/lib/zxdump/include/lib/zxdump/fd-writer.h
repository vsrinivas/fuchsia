// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_FD_WRITER_H_
#define SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_FD_WRITER_H_

#include <lib/fit/result.h>
#include <sys/uio.h>

#include <string_view>
#include <vector>

#include <fbl/unique_fd.h>

#include "types.h"

// Like all zxdump APIs, the interfaces here are not thread-safe.

namespace zxdump {

// This provides callbacks for using zxdump::ProcessDump to stream output to
// a file descriptor.  It supports both seekable and non-seekable descriptors.
//
// Writers work in two models: accumulate and flush for small fragments; and
// direct writing for large chunks.  The first model is used for the headers
// and notes, which come in many small pieces that can be collected together in
// a single writev call.  The second model is used for the bulk data like
// memory segments, which is streamed through temporary buffers rather than
// held in the dumper's memory throughout, but comes in large chunks big enough
// to merit individual write calls.
class FdWriter {
 public:
  // On failure, the error value is a string saying what operation on
  // on the fd failed and its error is still in errno.
  using error_type = std::string_view;

  FdWriter() = delete;
  FdWriter(FdWriter&&) = default;
  FdWriter& operator=(FdWriter&&) = default;

  // The writer takes ownership of the fd.
  explicit FdWriter(fbl::unique_fd fd) : fd_(std::move(fd)) {}

  // Pass the result of this to zxdump::ProcessDump::DumpHeaders or
  // zxdump::JobDump::DumpHeaders.  The callback accumulates small fragments to
  // be written out by WriteFragments.  The views passed to the callback must
  // stay valid pointers until after WriteFragments returns.  This callback
  // expects to receive a contiguous stream of data with no gaps before each
  // offset.
  //
  // The returned callable object is valid for the lifetime of the FdWriter.
  auto AccumulateFragmentsCallback() {
    return [this](size_t offset, ByteView data) -> fit::result<error_type> {
      Accumulate(offset, data);
      return fit::ok();
    };
  }

  // Call this after DumpHeaders makes all its calls to that callback, and
  // before calling the WriteCallback callback.  It returns the number of bytes
  // written out.
  fit::result<error_type, size_t> WriteFragments();

  // Pass the result of this to zxdump::ProcessDump::DumpMemory or the like.
  // The callback makes direct writes.  It accepts an offset that advances over
  // a gap since the preceding write (either via this callback or via the
  // previous WriteFragments call), but offsets can never go backwards.
  //
  // The returned callable object is valid for the lifetime of the FdWriter.
  auto WriteCallback() {
    return [this](size_t offset, ByteView data) -> fit::result<error_type> {
      return Write(offset, data);
    };
  }

  // Reset the file offset calculations.  After this, the next call to one of
  // the callbacks is expected to use offset 0.
  void ResetOffset() { total_ = 0; }

 private:
  struct Fragments {
    std::vector<iovec> iov_;
    size_t size_bytes_ = 0;
  };

  // Just store the data pointer for WriteFragments to gather later.
  void Accumulate(size_t offset, ByteView data);

  // Directly write the data out, seeking or zero-padding ahead if there's a
  // gap from the last write to this offset.
  fit::result<error_type> Write(size_t offset, ByteView data);

  Fragments fragments_;
  size_t total_ = 0;
  fbl::unique_fd fd_;
  bool is_pipe_ = false;
};

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_FD_WRITER_H_
