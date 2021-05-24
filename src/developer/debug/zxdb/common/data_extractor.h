// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_DATA_EXTRACTOR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_DATA_EXTRACTOR_H_

#include <string.h>

#include <algorithm>
#include <optional>

#include "src/lib/containers/cpp/array_view.h"

namespace zxdb {

class DataExtractor {
 public:
  DataExtractor() = default;
  explicit DataExtractor(containers::array_view<uint8_t> data) : data_(data) {}

  // Returns the current position in the buffer.
  size_t cur() const { return cur_; }

  // Returns true is there is more data to read.
  bool done() const { return cur_ >= data_.size(); }

  // Reads the given value, returning it if there is room, and advancing the current location. If
  // there is not enough bytes, the current position will remain unchanged and a nullopt will be
  // returned.
  //
  // Normally one would read an explicitly sized value so the result doesn't depend on the current
  // machine:
  //
  //   auto result = extractor.Read<uint32_t>();
  template <typename T>
  std::optional<T> Read() {
    T result;
    if (!ReadBytes(sizeof(T), &result))
      return std::nullopt;
    return result;
  }

  // Rturns true if there are at least the number of remaining bytes in the buffer.
  bool CanRead(size_t bytes) const {
    return bytes <= data_.size() &&  // Prevent overflow of subtraction below.
           data_.size() - cur_ >= bytes;
  }

  // Advances the current location by the given number of bytes. If it advances past the end, it
  // will stop there.
  void Advance(size_t bytes) { cur_ = std::min(cur_ + bytes, data_.size()); }

  // Sets the current location to the given absolute index. If it advances past the end, it will
  // stop there.
  void Seek(size_t new_offset) { cur_ = std::min(data_.size(), new_offset); }

  // Copies the given number of bytes into the |dest| buffer and advances the current position.
  // Returns true on success. False means there weren't enough bytes to read.
  bool ReadBytes(size_t bytes, void* dest) {
    if (!CanRead(bytes))
      return false;

    if (bytes) {
      memcpy(dest, &data_[cur_], bytes);
      cur_ += bytes;
    }
    return true;
  }

  // Reads a DWARF signed or unsigned "LEB128"-encoded value from the stream. This encoding is a
  // UTF-8-like variable-length integer encoding.
  std::optional<int64_t> ReadSleb128();
  std::optional<uint64_t> ReadUleb128();

 private:
  containers::array_view<uint8_t> data_;
  size_t cur_ = 0;  // Current index in data_.
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_DATA_EXTRACTOR_H_
