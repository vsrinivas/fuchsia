// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_AV_IO_CONTEXT_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_AV_IO_CONTEXT_H_

#include <condition_variable>
#include <mutex>

#include "apps/media/services/framework/parts/reader.h"
extern "C" {
#include "third_party/ffmpeg/libavformat/avio.h"
}

namespace mojo {
namespace media {

struct AVIOContextDeleter {
  void operator()(AVIOContext* context) const;
};

using AvIoContextPtr = std::unique_ptr<AVIOContext, AVIOContextDeleter>;

class AvIoContext {
 public:
  // Creates an ffmpeg avio_context for a given reader.
  static AvIoContextPtr Create(std::shared_ptr<Reader> reader);
};

// 'Opaque' context bound to ffmpeg AVIOContext.
//
// AvIoContextOpaque is instantiated when an AVIOContext is created and is bound
// to the AVIOContext via the 'opaque' field. AvIoContextOpaque's purpose is to
// translate read and seek requests from ffmpeg into terms that make sense for
// the framework. The principal issue is that ffmpeg issues synchronous read and
// seek requests (static Read and Seek below), and the framework exposes these
// capabilities as an asynchronous request (Reader::ReadAt).
//
// AvIoContextOpaque implements synchronous Read requests by issuing an
// asynchronous request and waiting for the callback to be invoked. The wait is
// done using a mutex and a condition_variable. There's no attempt to pump and
// message queues during the wait, so the the ReadAt callback will be on a
// different thread than the synchronous request.
class AvIoContextOpaque {
 public:
  // Performs a read operation using the signature required for avio.
  static int Read(void* opaque, uint8_t* buf, int buf_size);

  // Performs a seek operation using the signature required for avio.
  static int64_t Seek(void* opaque, int64_t offset, int whence);

  ~AvIoContextOpaque();

 private:
  AvIoContextOpaque(std::shared_ptr<Reader> reader);

  // Indicates whether the reader can seek
  bool can_seek() { return can_seek_; }

  // Performs a synchronous read.
  int Read(uint8_t* buffer, size_t bytes_to_read);

  // Performs a synchronous seek.
  int64_t Seek(int64_t offset, int whence);

  void WaitForCallback() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!callback_happened_) {
      condition_variable_.wait(lock);
    }
    callback_happened_ = false;
  }

  void CallbackComplete() {
    std::unique_lock<std::mutex> lock(mutex_);
    DCHECK(!callback_happened_);
    callback_happened_ = true;
    condition_variable_.notify_all();
  }

  std::shared_ptr<Reader> reader_;
  Result describe_result_;
  int64_t size_;
  bool can_seek_;
  int64_t position_ = 0;
  std::mutex mutex_;
  std::condition_variable condition_variable_;
  bool callback_happened_ = false;

  friend class AvIoContext;
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_AV_IO_CONTEXT_H_
