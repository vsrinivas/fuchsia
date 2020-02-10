// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_WRITE_ONLY_FILE_SIZE_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_WRITE_ONLY_FILE_SIZE_H_

#include <cstdint>

namespace feedback {

class FileSize {
 public:
  static constexpr inline FileSize Bytes(uint64_t bytes) { return FileSize(bytes); }

  static constexpr inline FileSize Megabytes(uint64_t megabytes) {
    // Mask out the top 20 bits of of the passed value to prevent overflow when shifting.
    constexpr uint64_t mask{0x00000FFFFFFFFFFF};
    return FileSize((megabytes & mask) << 20);
  }

  static constexpr inline FileSize Kilobytes(uint64_t kilobytes) {
    // Mask out the top 10 bits of of the passed value to prevent overflow when shifting.
    constexpr uint64_t mask{0x003FFFFFFFFFFFFF};
    return FileSize((kilobytes & mask) << 10);
  }
  constexpr uint64_t to_bytes() const { return bytes_; }
  constexpr uint64_t to_kb() const { return bytes_ >> 10; }
  constexpr uint64_t to_mb() const { return bytes_ >> 20; }

  constexpr FileSize operator-(FileSize other) const { return FileSize(bytes_ - other.bytes_); }
  constexpr FileSize operator+(FileSize other) const { return FileSize(bytes_ + other.bytes_); }
  constexpr FileSize operator*(FileSize other) const { return FileSize(bytes_ * other.bytes_); }
  constexpr FileSize operator/(FileSize other) const { return FileSize(bytes_ / other.bytes_); }

  constexpr FileSize operator-(uint64_t bytes) const { return FileSize(bytes_ - bytes); }
  constexpr FileSize operator+(uint64_t bytes) const { return FileSize(bytes_ + bytes); }
  constexpr FileSize operator*(uint64_t bytes) const { return FileSize(bytes_ * bytes); }
  constexpr FileSize operator/(uint64_t bytes) const { return FileSize(bytes_ / bytes); }

  constexpr FileSize& operator+=(FileSize other) {
    bytes_ += other.bytes_;
    return *this;
  }

  constexpr FileSize& operator+=(uint64_t bytes) {
    bytes_ += bytes;
    return *this;
  }

  constexpr FileSize& operator-=(FileSize other) {
    bytes_ -= other.bytes_;
    return *this;
  }

  constexpr FileSize& operator-=(uint64_t bytes) {
    bytes_ -= bytes;
    return *this;
  }

 private:
  explicit constexpr FileSize(uint64_t bytes) : bytes_(bytes) {}

  uint64_t bytes_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_WRITE_ONLY_FILE_SIZE_H_
