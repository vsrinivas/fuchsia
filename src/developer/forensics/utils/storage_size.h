// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_STORAGE_SIZE_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_STORAGE_SIZE_H_

#include <cstdint>
#include <numeric>

namespace forensics {

// Size class for storage mediums, like files, with associated methods to convert between bytes,
// kilobytes, megabytes, and gigabytes.
//
// Note: This class draws heavily from zx::duration, but unlike zx::duration it does nothing to
// prevent integer under/over flow and should be used with caution.
class StorageSize final {
 public:
  constexpr StorageSize() = default;

  explicit constexpr StorageSize(uint64_t bytes) : bytes_(bytes) {}

  static constexpr StorageSize Bytes(uint64_t bytes) { return StorageSize(bytes); }
  static constexpr StorageSize Kilobytes(uint64_t kilobytes) {
    return StorageSize(kilobytes << 10);
  }
  static constexpr StorageSize Megabytes(uint64_t megabytes) {
    return StorageSize(megabytes << 20);
  }
  static constexpr StorageSize Gigabytes(uint64_t gigabytes) {
    return StorageSize(gigabytes << 30);
  }

  constexpr uint64_t Get() const { return bytes_; }

  constexpr StorageSize operator+(StorageSize other) const {
    return StorageSize(bytes_ + other.bytes_);
  }

  constexpr StorageSize operator-(StorageSize other) const {
    return StorageSize(bytes_ - other.bytes_);
  }

  constexpr StorageSize operator*(uint64_t scalar) const { return StorageSize(bytes_ * scalar); }

  constexpr uint64_t operator/(StorageSize other) const { return bytes_ / other.bytes_; }
  constexpr StorageSize operator/(uint64_t scalar) const { return StorageSize(bytes_ / scalar); }

  constexpr StorageSize& operator+=(StorageSize other) {
    bytes_ += other.bytes_;
    return *this;
  }

  constexpr StorageSize& operator-=(StorageSize other) {
    bytes_ -= other.bytes_;
    return *this;
  }

  constexpr StorageSize& operator*=(uint64_t scalar) {
    bytes_ *= scalar;
    return *this;
  }

  constexpr StorageSize& operator/=(uint64_t scalar) {
    bytes_ /= scalar;
    return *this;
  }

  constexpr bool operator==(StorageSize other) const { return bytes_ == other.bytes_; }
  constexpr bool operator!=(StorageSize other) const { return bytes_ != other.bytes_; }
  constexpr bool operator<(StorageSize other) const { return bytes_ < other.bytes_; }
  constexpr bool operator<=(StorageSize other) const { return bytes_ <= other.bytes_; }
  constexpr bool operator>(StorageSize other) const { return bytes_ > other.bytes_; }
  constexpr bool operator>=(StorageSize other) const { return bytes_ >= other.bytes_; }

  constexpr uint64_t ToBytes() const { return bytes_; }
  constexpr uint64_t ToKilobytes() const { return bytes_ >> 10; }
  constexpr uint64_t ToMegabytes() const { return bytes_ >> 20; }
  constexpr uint64_t ToGigabytes() const { return bytes_ >> 30; }

 private:
  uint64_t bytes_{0};
};

template <typename T>
constexpr StorageSize operator*(const T scalar, StorageSize size) {
  static_assert(std::numeric_limits<T>::is_integer);
  return StorageSize(size.Get() * scalar);
}

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_STORAGE_SIZE_H_
