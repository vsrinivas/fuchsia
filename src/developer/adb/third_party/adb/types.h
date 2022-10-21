/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_TYPES_H_
#define SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_TYPES_H_

#include <string.h>
#include <zircon/assert.h>

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

// Essentially std::vector<char>, except without zero initialization or reallocation.
struct Block {
  using iterator = char*;

  Block() = default;

  explicit Block(size_t size) { allocate(size); }

  template <typename Iterator>
  Block(Iterator begin, Iterator end) : Block(end - begin) {
    std::copy(begin, end, data_.get());
  }

  Block(const Block& copy) = delete;
  Block(Block&& move) noexcept
      : data_(std::exchange(move.data_, nullptr)),
        capacity_(std::exchange(move.capacity_, 0)),
        size_(std::exchange(move.size_, 0)) {}

  Block& operator=(const Block& copy) = delete;
  Block& operator=(Block&& move) noexcept {
    clear();
    data_ = std::exchange(move.data_, nullptr);
    capacity_ = std::exchange(move.capacity_, 0);
    size_ = std::exchange(move.size_, 0);
    return *this;
  }

  ~Block() = default;

  void resize(size_t new_size) {
    if (!data_) {
      allocate(new_size);
    } else {
      ZX_ASSERT(capacity_ >= new_size);
      size_ = new_size;
    }
  }

  template <typename InputIt>
  void assign(InputIt begin, InputIt end) {
    clear();
    allocate(end - begin);
    std::copy(begin, end, data_.get());
  }

  void clear() {
    data_.reset();
    capacity_ = 0;
    size_ = 0;
  }

  size_t capacity() const { return capacity_; }
  size_t size() const { return size_; }
  bool empty() const { return size() == 0; }

  char* data() { return data_.get(); }
  const char* data() const { return data_.get(); }

  char* begin() { return data_.get(); }
  const char* begin() const { return data_.get(); }

  char* end() { return data() + size_; }
  const char* end() const { return data() + size_; }

  char& operator[](size_t idx) { return data()[idx]; }
  const char& operator[](size_t idx) const { return data()[idx]; }

  bool operator==(const Block& rhs) const {
    return size() == rhs.size() && memcmp(data(), rhs.data(), size()) == 0;
  }

 private:
  void allocate(size_t size) {
    ZX_ASSERT(data_ == nullptr);
    ZX_ASSERT(0ULL == capacity_);
    ZX_ASSERT(0ULL == size_);
    if (size != 0) {
      // This isn't std::make_unique because that's equivalent to `new char[size]()`, which
      // value-initializes the array instead of leaving it uninitialized. As an optimization,
      // call new without parentheses to avoid this costly initialization.
      data_.reset(new char[size]);
      capacity_ = size;
      size_ = size;
    }
  }

  std::unique_ptr<char[]> data_;
  size_t capacity_ = 0;
  size_t size_ = 0;
};

struct amessage {
  uint32_t command;     /* command identifier constant      */
  uint32_t arg0;        /* first argument                   */
  uint32_t arg1;        /* second argument                  */
  uint32_t data_length; /* length of payload (0 is allowed) */
  uint32_t data_check;  /* checksum of data payload         */
  uint32_t magic;       /* command ^ 0xffffffff             */
};

struct apacket {
  using payload_type = Block;
  amessage msg;
  payload_type payload;
};

// Service Names
constexpr std::string_view kShellService = "SHELL";
constexpr std::string_view kFfxService = "FFX";
constexpr std::string_view kFileSyncService = "FILE_SYNC";

#endif  // SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_TYPES_H_
