// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_BUFFER_POOL_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_BUFFER_POOL_H_

extern "C" {
#include "libavcodec/avcodec.h"
}

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <src/lib/fxl/synchronization/thread_annotations.h>
#include <lib/media/codec_impl/codec_buffer.h>

#include "mpsc_queue.h"

// BufferPool manages buffers for backing AVFrames and integrates with Ffmpeg's
// refcounting system.
class BufferPool {
 public:
  enum Status {
    OK = 0,
    UNSUPPORTED_FOURCC = 1,
    SHUTDOWN = 2,
    FILL_ARRAYS_FAILED = 3
  };

  // Describes the requirements of a buffer which can back a frame.
  struct FrameBufferRequest {
    fuchsia::media::VideoUncompressedFormat format;
    size_t buffer_bytes_needed;
  };

  struct Allocation {
    const CodecBuffer* buffer;
    size_t bytes_used;
  };

  // Configures an AVFrame to point at a buffer, including the logic to
  // point at each plane.
  Status AttachFrameToBuffer(AVFrame* frame,
                             const FrameBufferRequest& frame_buffer_request,
                             int flags, const CodecBuffer* buffer = nullptr);

  void AddBuffer(const CodecBuffer* buffer);

  // Looks up what buffer from the pool backs a frame Ffmpeg has output.
  std::optional<Allocation> FindBufferByFrame(AVFrame* frame);

  // Removes all free buffers and re-arms the buffer pool to block when
  // servicing frame attachment requests.
  //
  // Does not modify the tracking for buffers already in use by Ffmpeg.
  void Reset(bool keep_data = false);

  // Stop blocking for new buffers when empty.
  void StopAllWaits();

  // Returns whether Ffmpeg is using any buffers in the pool.
  bool has_buffers_in_use();

 private:
  // Reads the opaque pointer from our free callback and routes it to our
  // instance. The opaque pointer is provided when we set up a free callback
  // when providing buffers to the decoder in GetBuffer.
  static void BufferFreeCallbackRouter(void* opaque, uint8_t* data);

  // A callback handler for when buffers are freed by the decoder, which returns
  // them to our pool. The opaque pointer is provided when we set up a free
  // callback when providing buffers to the decoder in GetBuffer.
  void BufferFreeHandler(uint8_t* data);

  std::mutex lock_;
  std::map<uint8_t*, Allocation> buffers_in_use_ FXL_GUARDED_BY(lock_);
  BlockingMpscQueue<const CodecBuffer*> free_buffers_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_BUFFER_POOL_H_
