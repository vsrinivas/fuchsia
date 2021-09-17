// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/input.h"

#include <errno.h>
#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/status.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace fuzzing {

void Input::Allocate(size_t capacity, const void* data, size_t size) {
  size_ = std::min(capacity, size);
  capacity_ = capacity;
  if (capacity_) {
    data_ = std::make_unique<uint8_t[]>(capacity_);
  } else {
    data_.reset();
  }
  if (data && size_) {
    memcpy(data_.get(), data, size_);
  }
}

Input& Input::operator=(Input&& other) noexcept {
  data_ = std::move(other.data_);
  capacity_ = other.capacity_;
  size_ = other.size_;
  num_features_ = other.num_features_;
  other.capacity_ = 0;
  other.size_ = 0;
  other.num_features_ = 0;
  return *this;
}

bool Input::operator==(const Input& other) const {
  return size_ == other.size_ && memcmp(data_.get(), other.data_.get(), size_) == 0;
}

std::string Input::ToHex() const {
  std::stringstream oss;
  oss << std::hex;
  auto* data = data_.get();
  for (size_t i = 0; i < size_; ++i) {
    oss << std::setw(2) << std::setfill('0') << size_t(data[i]);
  }
  return oss.str();
}

void Input::Swap(Input& other) {
  data_.swap(other.data_);
  std::swap(capacity_, other.capacity_);
  std::swap(size_, other.size_);
  std::swap(num_features_, other.num_features_);
}

Input Input::Duplicate() const {
  Input other;
  other.Allocate(size_, data_.get(), size_);
  other.num_features_ = num_features_;
  return other;
}

void Input::Reserve(size_t capacity) {
  if (capacity_ < capacity) {
    auto tmp = std::move(data_);
    Allocate(capacity, tmp.get(), size_);
  }
}

void Input::Write(const void* data, size_t size) {
  FX_DCHECK(size_ + size <= capacity_);
  memcpy(&data_[size_], data, size);
  size_ += size;
}

void Input::Write(uint8_t one_byte) {
  FX_DCHECK(size_ < capacity_);
  data_[size_++] = one_byte;
}

size_t Input::Resize(size_t size) {
  Reserve(size);
  size_ = size;
  return size_;
}

size_t Input::Truncate(size_t max_size) {
  size_ = std::min(size_, max_size);
  return size_;
}

size_t Input::ShrinkToFit() {
  if (size_ != capacity_) {
    auto tmp = std::move(data_);
    Allocate(size_, tmp.get(), size_);
  }
  return size_;
}

void Input::Clear() { size_ = 0; }

}  // namespace fuzzing
