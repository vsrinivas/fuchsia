// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "apps/media/src/ffmpeg/av_io_context.h"

#include "apps/media/src/demux/reader.h"
#include "apps/media/src/ffmpeg/ffmpeg_init.h"
#include "lib/ftl/logging.h"
extern "C" {
#include "third_party/ffmpeg/libavformat/avio.h"
}

namespace media {

void AVIOContextDeleter::operator()(AVIOContext* context) const {
  AvIoContextOpaque* av_io_context =
      reinterpret_cast<AvIoContextOpaque*>(context->opaque);
  FTL_DCHECK(av_io_context);
  delete av_io_context;
  av_free(context->buffer);
  av_free(context);
}

// static
Result AvIoContext::Create(std::shared_ptr<Reader> reader,
                           AvIoContextPtr* context_ptr_out) {
  FTL_CHECK(context_ptr_out);

  // Internal buffer size used by AVIO for reading.
  constexpr int kBufferSize = 32 * 1024;

  InitFfmpeg();

  AvIoContextOpaque* avIoContextOpaque = new AvIoContextOpaque(reader);
  Result result = avIoContextOpaque->describe_result_;
  if (result != Result::kOk) {
    *context_ptr_out = nullptr;
    delete avIoContextOpaque;
    return result;
  }

  AVIOContext* avIoContext = avio_alloc_context(
      static_cast<unsigned char*>(av_malloc(kBufferSize)), kBufferSize,
      0,  // write_flag
      avIoContextOpaque, &AvIoContextOpaque::Read, nullptr,
      &AvIoContextOpaque::Seek);

  // Ensure FFmpeg only tries to seek when we know how.
  avIoContext->seekable =
      avIoContextOpaque->can_seek() ? AVIO_SEEKABLE_NORMAL : 0;

  // Ensure writing is disabled.
  avIoContext->write_flag = 0;

  *context_ptr_out = AvIoContextPtr(avIoContext);

  return result;
}

// static
int AvIoContextOpaque::Read(void* opaque, uint8_t* buf, int buf_size) {
  AvIoContextOpaque* av_io_context =
      reinterpret_cast<AvIoContextOpaque*>(opaque);
  return av_io_context->Read(buf, buf_size);
}

// static
int64_t AvIoContextOpaque::Seek(void* opaque, int64_t offset, int whence) {
  AvIoContextOpaque* av_io_context =
      reinterpret_cast<AvIoContextOpaque*>(opaque);
  return av_io_context->Seek(offset, whence);
}

AvIoContextOpaque::~AvIoContextOpaque() {}

AvIoContextOpaque::AvIoContextOpaque(std::shared_ptr<Reader> reader)
    : reader_(reader) {
  reader->Describe([this](Result result, size_t size, bool can_seek) {
    describe_result_ = result;
    size_ = size == Reader::kUnknownSize ? -1 : static_cast<int64_t>(size);
    can_seek_ = can_seek;
    CallbackComplete();
  });

  WaitForCallback();
}

int AvIoContextOpaque::Read(uint8_t* buffer, size_t bytes_to_read) {
  FTL_DCHECK(position_ >= 0);

  if (position_ >= size_) {
    return 0;
  }

  FTL_DCHECK(static_cast<uint64_t>(position_) <
             std::numeric_limits<size_t>::max());

  Result read_at_result;
  size_t read_at_bytes_read;
  reader_->ReadAt(static_cast<size_t>(position_), buffer, bytes_to_read,
                  [this, &read_at_result, &read_at_bytes_read](
                      Result result, size_t bytes_read) {
                    read_at_result = result;
                    read_at_bytes_read = bytes_read;
                    CallbackComplete();
                  });

  WaitForCallback();

  if (read_at_result != Result::kOk) {
    FTL_LOG(ERROR) << "read failed";
    return AVERROR(EIO);
  }

  position_ += read_at_bytes_read;
  return read_at_bytes_read;
}

int64_t AvIoContextOpaque::Seek(int64_t offset, int whence) {
  switch (whence) {
    case SEEK_SET:
      position_ = offset;
      break;
    case SEEK_CUR:
      position_ += offset;
      break;
    case SEEK_END:
      if (size_ == -1) {
        FTL_LOG(WARNING) << "whence of SEEK_END, size unknown";
        return AVERROR(EIO);
      }
      position_ = size_ + offset;
      break;
    case AVSEEK_SIZE:
      if (size_ == -1) {
        FTL_LOG(WARNING) << "whence of AVSEEK_SIZE, size unknown";
        return AVERROR(EIO);
      }
      return size_;
    default:
      FTL_LOG(ERROR) << "unrecognized whence value " << whence;
      return AVERROR(EIO);
  }

  FTL_CHECK(size_ == -1 || position_ < size_) << "position out of range";
  return position_;
}

}  // namespace media
