// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_MEMORY_MAPPED_BUFFER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_MEMORY_MAPPED_BUFFER_H_

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/status.h>

#include <memory>

namespace media_audio {

// A simple wrapper around a VMO that is used as a payload buffer for audio data. Typically there
// are many packets per buffer. The buffer should be `writable` iff the buffer will be written by
// the mixer service, such as when producing captured audio.
class MemoryMappedBuffer {
 public:
  // Creates a MemoryMappedBuffer from the given VMO.
  static zx::status<std::shared_ptr<MemoryMappedBuffer>> Create(const zx::vmo& vmo, bool writable);

  // Returns the start address of the buffer.
  void* start() const { return mapper_.start(); }

  // Returns the end address of the buffer.
  void* end() const { return static_cast<char*>(start()) + size(); }

  // Returns a byte offset within this buffer.
  void* offset(size_t bytes_from_start) const {
    return static_cast<char*>(start()) + bytes_from_start;
  }

  // Returns the size of the buffer in bytes.
  size_t size() const { return mapper_.size(); }

 private:
  explicit MemoryMappedBuffer(fzl::VmoMapper mapper) : mapper_(std::move(mapper)) {}

  MemoryMappedBuffer(const MemoryMappedBuffer&) = delete;
  MemoryMappedBuffer& operator=(const MemoryMappedBuffer&) = delete;
  MemoryMappedBuffer(MemoryMappedBuffer&&) = delete;
  MemoryMappedBuffer& operator=(MemoryMappedBuffer&&) = delete;

  fzl::VmoMapper mapper_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_MEMORY_MAPPED_BUFFER_H_
