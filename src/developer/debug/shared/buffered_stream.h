// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_STREAM_H_
#define SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_STREAM_H_

#include <functional>

#include "src/developer/debug/shared/stream_buffer.h"

namespace debug {

// A base class for implementation-specific version of a buffered stream.
//
// This manages a StreamBuffer for the actual buffering, and provides a common API for managing
// the stream. Its derived classes supply the actual reading and writing APIs.
class BufferedStream : public StreamBuffer::Writer {
 public:
  using DataAvailableCallback = std::function<void()>;
  using ErrorCallback = std::function<void()>;

  BufferedStream() { stream_.set_writer(this); }
  virtual ~BufferedStream() = default;

  // Starts and stops listening to the stream source.
  virtual bool Start() = 0;
  virtual bool Stop() = 0;

  // Stops, releases the resources, and resets all callbacks.
  void Reset() {
    ResetInternal();
    callback_ = DataAvailableCallback();
    error_callback_ = ErrorCallback();
  }

  // Returns true if the stream is properly set up.
  virtual bool IsValid() = 0;

  void set_data_available_callback(DataAvailableCallback cb) { callback_ = std::move(cb); }
  void set_error_callback(ErrorCallback cb) { error_callback_ = std::move(cb); }

  StreamBuffer& stream() { return stream_; }
  const StreamBuffer& stream() const { return stream_; }

 protected:
  // Does the stream-specific work of Reset().
  virtual void ResetInternal() = 0;

  [[nodiscard]] DataAvailableCallback& callback() { return callback_; }
  [[nodiscard]] ErrorCallback& error_callback() { return error_callback_; }

 private:
  StreamBuffer stream_;

  DataAvailableCallback callback_;
  ErrorCallback error_callback_;
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_STREAM_H_
