// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/ffmpeg/av_io_context.h"

#include <limits>

#include "src/media/playback/mediaplayer/demux/reader.h"
#include "src/media/playback/mediaplayer/ffmpeg/ffmpeg_init.h"
extern "C" {
#include "libavformat/avio.h"
#include "libavutil/mem.h"
}

namespace media_player {

void AVIOContextDeleter::operator()(AVIOContext* context) const {
  AvIoContextOpaque* av_io_context = reinterpret_cast<AvIoContextOpaque*>(context->opaque);
  FX_DCHECK(av_io_context);
  // This is the matching delete for the new that happens in
  // AvIoContext::Create. This is part of the deleter for the io context, which
  // is managed by AvIoContextPtr, a unique_ptr that uses this deleter.
  delete av_io_context;
  av_free(context->buffer);
  av_free(context);
}

// static
zx_status_t AvIoContext::Create(std::shared_ptr<Reader> reader, AvIoContextPtr* context_ptr_out,
                                async_dispatcher_t* dispatcher) {
  FX_CHECK(context_ptr_out);

  // Internal buffer size used by AVIO for reading.
  constexpr int kBufferSize = 32 * 1024;

  InitFfmpeg();

  // Using a raw pointer here, because the io context doesn't understand smart
  // pointers.
  AvIoContextOpaque* avIoContextOpaque = new AvIoContextOpaque(reader, dispatcher);
  zx_status_t status = avIoContextOpaque->describe_status_;
  if (status != ZX_OK) {
    *context_ptr_out = nullptr;
    delete avIoContextOpaque;
    return status;
  }

  AVIOContext* avIoContext = avio_alloc_context(
      static_cast<unsigned char*>(av_malloc(kBufferSize)), kBufferSize,
      0,  // write_flag
      avIoContextOpaque, &AvIoContextOpaque::Read, nullptr, &AvIoContextOpaque::Seek);

  // Ensure FFmpeg only tries to seek when we know how.
  avIoContext->seekable = avIoContextOpaque->can_seek() ? AVIO_SEEKABLE_NORMAL : 0;

  // Ensure writing is disabled.
  avIoContext->write_flag = 0;

  *context_ptr_out = AvIoContextPtr(avIoContext);

  return status;
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

AvIoContextOpaque::~AvIoContextOpaque() {}

AvIoContextOpaque::AvIoContextOpaque(std::shared_ptr<Reader> reader, async_dispatcher_t* dispatcher)
    : reader_(reader), dispatcher_(dispatcher) {
  async::PostTask(dispatcher_, [this]() {
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
  FX_DCHECK(position_ >= 0);

  if (position_ >= size_) {
    return AVERROR_EOF;
  }

  FX_DCHECK(static_cast<uint64_t>(position_) < std::numeric_limits<size_t>::max());

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

  position_ += read_at_bytes_read;
  return read_at_bytes_read;
}

int64_t AvIoContextOpaque::Seek(int64_t offset, int whence) {
  switch (whence) {
    case SEEK_SET:
      if (size_ != -1 && offset >= size_) {
        FX_LOGS(ERROR) << "Seek out of range: offset " << offset << ", whence SEEK_SET, size "
                        << size_;
        return AVERROR(EIO);
      }

      position_ = offset;
      break;

    case SEEK_CUR:
      if (size_ != -1 && position_ + offset >= size_) {
        FX_LOGS(ERROR) << "Seek out of range: offset " << offset
                        << ", whence SEEK_CUR, current position " << position_ << ", size "
                        << size_;
        return AVERROR(EIO);
      }

      position_ += offset;
      break;

    case SEEK_END:
      if (size_ == -1) {
        FX_LOGS(ERROR) << "SEEK_END specified, size unknown";
        return AVERROR(EIO);
      }

      if (offset < -size_ || offset >= 0) {
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

  FX_DCHECK(size_ == -1 || position_ < size_);
  return position_;
}

}  // namespace media_player
