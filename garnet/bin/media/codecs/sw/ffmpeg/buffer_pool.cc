// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_pool.h"

extern "C" {
#include "libavutil/imgutils.h"
}

#include <src/lib/fxl/logging.h>
#include <lib/media/codec_impl/fourcc.h>

namespace {

AVPixelFormat FourccToPixelFormat(uint32_t fourcc) {
  switch (fourcc) {
    case make_fourcc('Y', 'V', '1', '2'):
      return AV_PIX_FMT_YUV420P;
    default:
      return AV_PIX_FMT_NONE;
  }
}

}  // namespace

BufferPool::Status BufferPool::AttachFrameToBuffer(
    AVFrame* frame,
    const FrameBufferRequest& frame_buffer_request, int flags,
    const CodecBuffer* buffer) {
  AVPixelFormat pix_fmt =
      FourccToPixelFormat(frame_buffer_request.format.fourcc);
  if (pix_fmt == AV_PIX_FMT_NONE) {
    return UNSUPPORTED_FOURCC;
  }

  if (!buffer) {
    std::optional<const CodecBuffer*> maybe_buffer = free_buffers_.WaitForElement();
    if (!maybe_buffer) {
      return SHUTDOWN;
    }
    buffer = *maybe_buffer;
  }

  AVBufferRef* buffer_ref = av_buffer_create(
      buffer->buffer_base(), static_cast<int>(buffer->buffer_size()),
      BufferFreeCallbackRouter, reinterpret_cast<void*>(this), flags);

  {
    std::lock_guard<std::mutex> lock(lock_);
    buffers_in_use_[buffer->buffer_base()] = {
        .buffer = buffer,
        .bytes_used = frame_buffer_request.buffer_bytes_needed,
    };
  }

  int status = av_image_fill_arrays(
      frame->data, frame->linesize, buffer_ref->data, pix_fmt,
      frame_buffer_request.format.primary_width_pixels,
      frame_buffer_request.format.primary_height_pixels, 1);
  if (status < 0) {
    return FILL_ARRAYS_FAILED;
  }

  // IYUV is not YV12. Ffmpeg only decodes into IYUV. The difference between
  // YV12 and IYUV is the order of the U and V planes. Here we trick Ffmpeg
  // into writing them in YV12 order relative to one another.
  std::swap(frame->data[1], frame->data[2]);

  frame->buf[0] = buffer_ref;
  // ffmpeg says to set extended_data to data if we're not using extended_data
  frame->extended_data = frame->data;

  return OK;
}

void BufferPool::AddBuffer(const CodecBuffer* buffer) {
  free_buffers_.Push(buffer);
}

std::optional<BufferPool::Allocation> BufferPool::FindBufferByFrame(
    AVFrame* frame) {
  std::lock_guard lock(lock_);
  auto iter = buffers_in_use_.find(frame->data[0]);
  if (iter == buffers_in_use_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

void BufferPool::Reset(bool keep_data) { free_buffers_.Reset(keep_data); }

void BufferPool::StopAllWaits() { free_buffers_.StopAllWaits(); }

bool BufferPool::has_buffers_in_use() {
  std::lock_guard<std::mutex> lock(lock_);
  return !buffers_in_use_.empty();
}

// static
void BufferPool::BufferFreeCallbackRouter(void* opaque, uint8_t* data) {
  auto decoder = reinterpret_cast<BufferPool*>(opaque);
  decoder->BufferFreeHandler(data);
}

void BufferPool::BufferFreeHandler(uint8_t* data) {
  BufferPool::Allocation allocation;
  fit::closure free_callback;
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto iter = buffers_in_use_.find(data);
    ZX_DEBUG_ASSERT(iter != buffers_in_use_.end());
    allocation = iter->second;
    buffers_in_use_.erase(data);
  }
  free_buffers_.Push(std::move(allocation.buffer));
}
