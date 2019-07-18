// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BUFFER_READER_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BUFFER_READER_H_

#include <optional>

#include <fbl/span.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace wlan {

class BufferReader {
 public:
  explicit BufferReader(fbl::Span<const uint8_t> buf) : buf_(buf) {}

  template <typename T>
  const T* Peek() {
    if (buf_.size() < offset_ + sizeof(T)) {
      return nullptr;
    }
    return reinterpret_cast<const T*>(buf_.data() + offset_);
  }

  template <typename T>
  const T* Read() {
    if (buf_.size() < offset_ + sizeof(T)) {
      return nullptr;
    }

    auto data = reinterpret_cast<const T*>(buf_.data() + offset_);
    offset_ += sizeof(T);
    return data;
  }

  template <typename T>
  fbl::Span<const T> ReadArray(size_t len) {
    if (buf_.size() < offset_ + sizeof(T) * len) {
      return {};
    }

    auto data = reinterpret_cast<const T*>(buf_.data() + offset_);
    offset_ += sizeof(T) * len;
    return {data, len};
  }

  template <typename T>
  std::optional<T> ReadValue() {
    if (auto t = Read<T>()) {
      return {*t};
    } else {
      return {};
    }
  }

  fbl::Span<const uint8_t> Read(size_t len) {
    if (buf_.size() < offset_ + len) {
      return {};
    }

    auto data = buf_.data() + offset_;
    offset_ += len;
    return {data, len};
  }

  fbl::Span<const uint8_t> ReadRemaining() {
    auto data = buf_.subspan(offset_);
    offset_ = buf_.size();
    return data;
  }

  size_t ReadBytes() const { return offset_; }
  size_t RemainingBytes() const { return buf_.size() - offset_; }

 private:
  fbl::Span<const uint8_t> buf_;
  size_t offset_ = 0;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BUFFER_READER_H_
