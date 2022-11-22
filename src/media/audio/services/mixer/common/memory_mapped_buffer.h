// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_MEMORY_MAPPED_BUFFER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_MEMORY_MAPPED_BUFFER_H_

#include <lib/fpromise/result.h>
#include <lib/fzl/vmo-mapper.h>

#include <memory>

namespace media_audio {

// A simple wrapper around a VMO that is used as a payload buffer for audio data. Typically there
// are many packets per buffer. The buffer should be `writable` iff the buffer will be written by
// the mixer service, such as when producing captured audio. The buffer may be DISCARDABLE but
// cannot be RESIZABLE.
class MemoryMappedBuffer {
 public:
  // Creates a MemoryMappedBuffer from the given object, which must be a valid, readable,
  // non-resizable, and mappable VMO, and if `writable`, the VMO must be writable. The
  // `size` must fit within the VMO.
  static fpromise::result<std::shared_ptr<MemoryMappedBuffer>, std::string> Create(
      const zx::vmo& vmo, size_t size, bool writable);

  // Like Create, but sets `size` to the full size of the VMO.
  static fpromise::result<std::shared_ptr<MemoryMappedBuffer>, std::string> CreateWithFullSize(
      const zx::vmo& vmo, bool writable);

  // Creates a MemoryMappedBuffer with the given size, crashing if the buffer cannot be created.
  static std::shared_ptr<MemoryMappedBuffer> CreateOrDie(size_t size, bool writable);

  // Returns the start address of the buffer.
  void* start() const { return mapper_.start(); }

  // Returns the end address of the buffer.
  void* end() const { return static_cast<char*>(start()) + size(); }

  // Returns a byte offset within this buffer.
  void* offset(size_t bytes_from_start) const {
    return static_cast<char*>(start()) + bytes_from_start;
  }

  // Returns the size of the buffer.
  size_t size() const { return size_; }

 private:
  MemoryMappedBuffer(fzl::VmoMapper mapper, size_t size)
      : mapper_(std::move(mapper)), size_(size) {}

  MemoryMappedBuffer(const MemoryMappedBuffer&) = delete;
  MemoryMappedBuffer& operator=(const MemoryMappedBuffer&) = delete;
  MemoryMappedBuffer(MemoryMappedBuffer&&) = delete;
  MemoryMappedBuffer& operator=(MemoryMappedBuffer&&) = delete;

  const fzl::VmoMapper mapper_;
  const size_t size_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_MEMORY_MAPPED_BUFFER_H_
