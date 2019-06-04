// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_BUFFER_POOL_H_
#define GARNET_BIN_MEDIA_CODECS_SW_BUFFER_POOL_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/media/codec_impl/codec_buffer.h>
#include <src/lib/fxl/synchronization/thread_annotations.h>

#include "mpsc_queue.h"

// BufferPool manages CodecBuffers for use with local output types in software
// encoders.
class BufferPool {
 public:
  struct Allocation {
    const CodecBuffer* buffer;
    size_t bytes_used;
  };

  void AddBuffer(const CodecBuffer* buffer);

  // Allocates a buffer for the caller and remembers the allocation size.
  const CodecBuffer* AllocateBuffer(size_t alloc_len = 0);

  // Frees a buffer by its base address, releasing it back to the pool.
  void FreeBuffer(uint8_t* base);

  // Looks up what buffer from the pool backs a frame Ffmpeg has output.
  std::optional<Allocation> FindBufferByBase(uint8_t* base);

  // Removes all free buffers and re-arms the buffer pool to block when
  // servicing allocation requests.
  //
  // Does not modify the tracking for buffers already in use.
  void Reset(bool keep_data = false);

  // Stop blocking for new buffers when empty.
  void StopAllWaits();

  // Returns whether any buffers in the pool are currently allocated.
  bool has_buffers_in_use();

 private:
  std::mutex lock_;
  std::map<uint8_t*, Allocation> buffers_in_use_ FXL_GUARDED_BY(lock_);
  BlockingMpscQueue<const CodecBuffer*> free_buffers_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_BUFFER_POOL_H_
