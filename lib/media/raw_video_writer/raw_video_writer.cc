// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "raw_video_writer.h"

#include <endian.h>

#include <fcntl.h>
#include <unistd.h>
#include <iomanip>
#include <string>

namespace media {

namespace {

constexpr const char* kDefaultFilePathName = "/tmp/raw_video_writer_";
constexpr const char* kFileExtension = ".raw_video";

}  // namespace

template <>
fbl::atomic<uint32_t> RawVideoWriter<true>::instance_count_(0u);

template <bool enabled>
RawVideoWriter<enabled>::RawVideoWriter(const char* file_name) {
  if (file_name) {
    file_name_ = file_name;
  } else {
    // mostly a comment
    FXL_DCHECK(file_name_.empty());
  }
}

template <bool enabled>
RawVideoWriter<enabled>::~RawVideoWriter() {
  if (!is_done_) {
    Close();
  }
}

template <bool enabled>
bool RawVideoWriter<enabled>::IsOk() {
  return is_ok_;
}

template <bool enabled>
size_t RawVideoWriter<enabled>::WriteUint32BigEndian(uint32_t number) {
  uint32_t to_write = htobe32(number);
  // May Fail() as appropriate.
  WriteData(reinterpret_cast<uint8_t*>(&to_write), sizeof(to_write));
  return sizeof(to_write);
}

template <bool enabled>
size_t RawVideoWriter<enabled>::WriteUint32LittleEndian(uint32_t number) {
  uint32_t to_write = htole32(number);
  // May Fail() as appropriate.
  WriteData(reinterpret_cast<uint8_t*>(&to_write), sizeof(to_write));
  return sizeof(to_write);
}

template <bool enabled>
size_t RawVideoWriter<enabled>::WriteNv12(uint32_t width_pixels,
                                          uint32_t height_pixels,
                                          uint32_t stride_bytes,
                                          const uint8_t* const y_base,
                                          uint32_t uv_offset) {
  // Despite the copy cost, for now let's perform the write as one big write, to
  // maximize the chance of getting a frame written or not written as a unit in
  // case of concurrent process exit or similar.  The copy cost isn't a huge
  // issue for the intended debugging purpose(s).
  size_t size = height_pixels * width_pixels + height_pixels / 2 * width_pixels;
  if (!y_base) {
    return size;
  }
  if (!uv_offset) {
    uv_offset = height_pixels * stride_bytes;
  }
  // This buffer isn't small.
  std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(size);
  uint8_t* dst = &buffer[0];
  const uint8_t* src = y_base;
  // Y
  for (uint32_t y_line = 0; y_line < height_pixels; y_line++) {
    memcpy(dst, src, width_pixels);
    dst += width_pixels;
    src += stride_bytes;
  }
  // UV
  // dest is already positioned correctly to write UV data.
  // src needs to account for a non-default uv_offset.
  src = y_base + uv_offset;
  for (uint32_t uv_line = 0; uv_line < height_pixels / 2; uv_line++) {
    memcpy(dst, src, width_pixels);
    dst += width_pixels;
    src += stride_bytes;
  }
  WriteData(&buffer[0], size);
  // ~buffer
  return size;
}

// Intentionally don't require IsOk().
template <bool enabled>
void RawVideoWriter<enabled>::Close() {
  // Caller shouldn't call Close() / Delete() repeatedly.
  if (is_done_) {
    Fail();
    return;
  }
  is_done_ = true;

  if (!is_initialized_) {
    // file never created, so nothing to do
    FXL_DCHECK(!file_.is_valid());
    return;
  }

  if (!file_.is_valid()) {
    FXL_DCHECK(!is_ok_);
    // Don't FXL_LOG(WARNING) again since we already did previously.
    return;
  }
  file_.reset();
  FXL_LOG(INFO) << "Closed raw video file " << std::quoted(file_name_);

  // is_ok_ intentionally not modified
}

// Intentionally don't require IsOk().
template <bool enabled>
void RawVideoWriter<enabled>::Delete() {
  // Caller shouldn't call Close() / Delete() repeatedly.
  if (is_done_) {
    Fail();
    return;
  }
  is_done_ = true;

  if (!is_initialized_) {
    // file never created, so nothing to do
    FXL_DCHECK(!file_.is_valid());
    return;
  }

  FXL_DCHECK(!file_name_.empty() || !file_.is_valid());
  if (!file_.is_valid()) {
    FXL_DCHECK(!is_ok_);
    // Don't FXL_LOG(WARNING) again since we already did previously.
    return;
  }
  file_.reset();

  if (::unlink(file_name_.c_str()) < 0) {
    FXL_LOG(WARNING) << "Could not delete " << std::quoted(file_name_);
    Fail();
    return;
  }

  FXL_LOG(INFO) << "Deleted raw video file " << std::quoted(file_name_);
}

template <bool enabled>
void RawVideoWriter<enabled>::EnsureInitialized() {
  if (!is_initialized_) {
    is_initialized_ = true;
    // If Initialize() actually fails, it's like a write failure later - is_ok_
    // gets set to false.
    Initialize();
  }
}

template <bool enabled>
void RawVideoWriter<enabled>::Initialize() {
  if (file_name_.empty()) {
    uint32_t instance_count = instance_count_.fetch_add(1);
    file_name_ = kDefaultFilePathName;
    file_name_ += std::to_string(instance_count) + kFileExtension;
  }
  file_.reset(::open(file_name_.c_str(), O_CREAT | O_WRONLY | O_TRUNC));
  if (!file_.is_valid()) {
    FXL_LOG(WARNING) << "::open failed for " << std::quoted(file_name_)
                     << ", returned " << file_.get() << ", errno " << errno;
    Fail();
    return;
  }
}

template <bool enabled>
void RawVideoWriter<enabled>::Fail() {
  FXL_LOG(WARNING) << "RawVideoWriter<enabled>::Fail()";
  is_ok_ = false;
  // We intentionally don't Close() or Delete() here - a client can do a Close()
  // or Delete() later without having looked at IsOk(), or might decide whether
  // to Close() or Delete() depending on IsOk()'s value.  The previous ::write()
  // calls should be effective at ensuring the previously written data is
  // preserved should this process exit before a Close() or Delete().
}

template <bool enabled>
void RawVideoWriter<enabled>::WriteData(const uint8_t* to_write, size_t size) {
  EnsureInitialized();
  if (!is_ok_) {
    // Don't FXL_LOG() again because we already did previously.
    return;
  }
  if (is_done_) {
    FXL_LOG(WARNING)
        << "RawVideoWriter write requested after Close() or Delete()";
    Fail();
    return;
  }
  FXL_DCHECK(file_.is_valid());
  ::write(file_.get(), reinterpret_cast<const void*>(to_write), size);
}

template class RawVideoWriter<true>;

}  // namespace media
