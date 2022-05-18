// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/ffmpeg/av_io_context.h"

#include <lib/async/default.h>

#include <limits>

#include "src/media/vnext/lib/ffmpeg/ffmpeg_init.h"
extern "C" {
#include "libavformat/avio.h"
}

namespace fmlib {

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
// done using a mutex and a condition_variable. There's no attempt to pump any
// message queues during the wait, so the the ReadAt callback will be on a
// different thread than the synchronous request.
class AvIoContextOpaque {
 public:
  // Performs a read operation using the signature required for avio.
  static int Read(void* opaque, uint8_t* buf, int buf_size);

  // Performs a seek operation using the signature required for avio.
  static int64_t Seek(void* opaque, int64_t offset, int whence);

  // Constructs an |AvIoContextOpaque|. This constructor blocks until |reader|'s |Describe| method
  // completes, at which point |this->describe_status()| is valid.
  AvIoContextOpaque(std::shared_ptr<Reader> reader, async_dispatcher_t* dispatcher);

  ~AvIoContextOpaque() = default;

  // Returns the status of the |Describe| call. The constructor blocks until this values is
  // established, so this method may be called any time after the constructor completes.
  zx_status_t describe_status() const { return describe_status_; }

  // Indicates whether the reader can seek
  bool can_seek() { return can_seek_; }

  // Performs a synchronous read.
  int Read(uint8_t* buffer, size_t bytes_to_read);

  // Performs a synchronous seek.
  int64_t Seek(int64_t offset, int whence);

 private:
  // Blocks the thread waiting for |CallbackComplete| to be called.
  // TODO(fxbug.dev/27120): Re-enable thread safety analysis once unique_lock has proper
  // annotations.
  void WaitForCallback() FXL_NO_THREAD_SAFETY_ANALYSIS {
    std::unique_lock<std::mutex> locker(mutex_);
    while (!callback_happened_) {
      condition_variable_.wait(locker);
    }
    callback_happened_ = false;
  }

  // Unblocks |WaitForCallback|.
  void CallbackComplete() {
    std::lock_guard<std::mutex> locker(mutex_);
    FX_CHECK(!callback_happened_);
    callback_happened_ = true;
    condition_variable_.notify_all();
  }

  std::shared_ptr<Reader> reader_;
  zx_status_t describe_status_;
  int64_t size_;
  bool can_seek_;
  int64_t position_ = 0;
  std::mutex mutex_;
  std::condition_variable condition_variable_;
  bool callback_happened_ FXL_GUARDED_BY(mutex_) = false;

  // For posting calls to FIDL thread.
  async_dispatcher_t* dispatcher_;
};

void AVIOContextDeleter::operator()(AVIOContext* context) const {
  AvIoContextOpaque* av_io_context = reinterpret_cast<AvIoContextOpaque*>(context->opaque);
  FX_CHECK(av_io_context);
  // This is the matching delete for the new that happens in
  // AvIoContext::Create. This is part of the deleter for the io context, which
  // is managed by AvIoContextPtr, a unique_ptr that uses this deleter.
  delete av_io_context;
  av_free(context->buffer);
  av_free(context);
}

// static
fpromise::result<AvIoContextPtr, zx_status_t> AvIoContext::Create(std::shared_ptr<Reader> reader,
                                                                  async_dispatcher_t* dispatcher) {
  FX_CHECK(reader);
  FX_CHECK(dispatcher);
  FX_CHECK(async_get_default_dispatcher() != dispatcher)
      << "dispatcher must not be the dispatcher for the calling thread.";

  // Internal buffer size used by AVIO for reading.
  constexpr int kBufferSize = 32 * 1024;

  InitFfmpeg();

  // Using a raw pointer here, because the io context doesn't understand smart
  // pointers.
  AvIoContextOpaque* avIoContextOpaque = new AvIoContextOpaque(std::move(reader), dispatcher);

  // The |AvIoContextOpaque| constructor blocks until the describe is done, so it's valid to call
  // |describe_status| at this point.
  zx_status_t status = avIoContextOpaque->describe_status();
  if (status != ZX_OK) {
    delete avIoContextOpaque;
    return fpromise::error(status);
  }

  AVIOContext* avIoContext = avio_alloc_context(
      static_cast<unsigned char*>(av_malloc(kBufferSize)), kBufferSize,
      0,  // write_flag
      avIoContextOpaque, &AvIoContextOpaque::Read, nullptr, &AvIoContextOpaque::Seek);

  // Ensure FFmpeg only tries to seek when we know how.
  avIoContext->seekable = avIoContextOpaque->can_seek() ? AVIO_SEEKABLE_NORMAL : 0;

  // Ensure writing is disabled.
  avIoContext->write_flag = 0;

  return fpromise::ok(AvIoContextPtr(avIoContext));
}

// static
int AvIoContextOpaque::Read(void* opaque, uint8_t* buf, int buf_size) {
  AvIoContextOpaque* av_io_context = reinterpret_cast<AvIoContextOpaque*>(opaque);
  return av_io_context->Read(buf, buf_size);
}

// static
int64_t AvIoContextOpaque::Seek(void* opaque, int64_t offset, int whence) {
  AvIoContextOpaque* av_io_context = reinterpret_cast<AvIoContextOpaque*>(opaque);
  return av_io_context->Seek(offset, whence);
}

AvIoContextOpaque::AvIoContextOpaque(std::shared_ptr<Reader> reader, async_dispatcher_t* dispatcher)
    : reader_(std::move(reader)), dispatcher_(dispatcher) {
  async::PostTask(dispatcher_, [this]() {
    // TODO(dalesat): change parameters so the NOLINT isn't needed.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    reader_->Describe([this](zx_status_t status, size_t size, bool can_seek) {
      describe_status_ = status;
      size_ = size == Reader::kUnknownSize ? -1 : static_cast<int64_t>(size);
      can_seek_ = can_seek;
      CallbackComplete();
    });
  });

  WaitForCallback();
}

int AvIoContextOpaque::Read(uint8_t* buffer, size_t bytes_to_read) {
  FX_CHECK(position_ >= 0);

  if (position_ > size_) {
    return AVERROR_EOF;
  }

  if (position_ == size_) {
    return 0;
  }

  FX_CHECK(static_cast<uint64_t>(position_) < std::numeric_limits<size_t>::max());

  zx_status_t read_at_status;
  size_t read_at_bytes_read;
  async::PostTask(
      dispatcher_, [this, &read_at_status, &read_at_bytes_read, buffer, bytes_to_read]() {
        reader_->ReadAt(
            static_cast<size_t>(position_), buffer, bytes_to_read,
            [this, &read_at_status, &read_at_bytes_read](zx_status_t status, size_t bytes_read) {
              read_at_status = status;
              read_at_bytes_read = bytes_read;
              CallbackComplete();
            });
      });

  WaitForCallback();

  if (read_at_status != ZX_OK) {
    FX_LOGS(ERROR) << "read failed";
    return AVERROR(EIO);
  }

  position_ += static_cast<int64_t>(read_at_bytes_read);
  return static_cast<int>(read_at_bytes_read);
}

// TODO(dalesat): change parameters so the NOLINT isn't needed.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int64_t AvIoContextOpaque::Seek(int64_t offset, int whence) {
  switch (whence) {
    case SEEK_SET:
      if (size_ != -1 && offset > size_) {
        FX_LOGS(ERROR) << "Seek out of range: offset " << offset << ", whence SEEK_SET, size "
                       << size_;
        return AVERROR(EIO);
      }

      position_ = offset;
      break;

    case SEEK_CUR:
      if (size_ != -1 && position_ + offset > size_) {
        FX_LOGS(ERROR) << "Seek out of range: offset " << offset
                       << ", whence SEEK_CUR, current position " << position_ << ", size " << size_;
        return AVERROR(EIO);
      }

      position_ += offset;
      break;

    case SEEK_END:
      if (size_ == -1) {
        FX_LOGS(ERROR) << "SEEK_END specified, size unknown";
        return AVERROR(EIO);
      }

      if (offset < -size_ || offset > 0) {
        FX_LOGS(ERROR) << "Seek out of range: offset " << offset << ", whence SEEK_END, size "
                       << size_;
        return AVERROR(EIO);
      }

      position_ = size_ + offset;
      break;

    case AVSEEK_SIZE:
      if (size_ == -1) {
        FX_LOGS(ERROR) << "AVSEEK_SIZE specified, size unknown";
        return AVERROR(EIO);
      }

      return size_;

    default:
      FX_LOGS(ERROR) << "unrecognized whence value " << whence;
      return AVERROR(EIO);
  }

  FX_CHECK(size_ == -1 || position_ <= size_);
  return position_;
}

}  // namespace fmlib
