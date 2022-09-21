// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_TESTS_SPAN_TESTS_H_
#define SRC_LIB_ZBITL_TESTS_SPAN_TESTS_H_

#include <lib/stdcompat/cstddef.h>
#include <lib/stdcompat/span.h>

#include <memory>
#include <string>
#include <string_view>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

template <typename T>
struct BasicStringViewTestTraits {
  using storage_type = std::basic_string_view<T>;
  using payload_type = std::basic_string_view<T>;

  static constexpr bool kDefaultConstructedViewHasStorageError = false;
  static constexpr bool kExpectExtensibility = false;
  static constexpr bool kExpectOneShotReads = true;
  static constexpr bool kExpectUnbufferedReads = true;
  static constexpr bool kExpectUnbufferedWrites = false;

  struct Context {
    storage_type TakeStorage() const {
      return {reinterpret_cast<const T*>(buff_.get()), size_ / sizeof(T)};
    }

    std::unique_ptr<std::byte[]> buff_;
    size_t size_ = 0;
  };

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    ASSERT_TRUE(fd);
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    std::unique_ptr<std::byte[]> buff{new std::byte[n]};
    EXPECT_EQ(static_cast<ssize_t>(n), read(fd.get(), buff.get(), n));
    *context = {std::move(buff), n};
  }

  static void Read(storage_type storage, payload_type payload, size_t size, std::string* contents) {
    *contents = {reinterpret_cast<const char*>(payload.data()), payload.size() * sizeof(T)};
  }
};

using StringTestTraits = BasicStringViewTestTraits<char>;

template <typename T>
struct SpanTestTraits {
  using storage_type = cpp20::span<T>;
  using payload_type = storage_type;

  static constexpr bool kDefaultConstructedViewHasStorageError = false;
  static constexpr bool kExpectExtensibility = false;
  static constexpr bool kExpectOneShotReads = true;
  static constexpr bool kExpectUnbufferedReads = true;
  static constexpr bool kExpectUnbufferedWrites = !std::is_const_v<T>;

  struct Context {
    storage_type TakeStorage() const {
      return {reinterpret_cast<T*>(buff_.get()), size_ / sizeof(T)};
    }

    std::unique_ptr<std::byte[]> buff_;
    size_t size_ = 0;
  };

  static void Create(size_t size, Context* context) {
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    std::unique_ptr<std::byte[]> buff{new std::byte[n]};
    *context = {std::move(buff), n};
  }

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    ASSERT_TRUE(fd);
    ASSERT_NO_FATAL_FAILURE(Create(size, context));
    EXPECT_EQ(static_cast<ssize_t>(context->size_),
              read(fd.get(), context->buff_.get(), context->size_));
  }

  static void Read(const storage_type& storage, payload_type payload, size_t size,
                   std::string* contents) {
    contents->resize(size);
    auto bytes = cpp20::as_bytes(payload);
    ASSERT_LE(size, bytes.size());
    memcpy(contents->data(), bytes.data(), size);
  }

  static void Write(storage_type& storage, uint32_t offset, const std::string& data) {
    ASSERT_LT(offset, storage.size());
    ASSERT_LE(offset, storage.size() - data.size());
    memcpy(storage.data() + offset, data.data(), data.size());
  }

  static void ToPayload(const storage_type& storage, uint32_t offset, payload_type& payload) {
    ASSERT_LT(offset, storage.size());
    payload = storage.subspan(offset);
  }
};

using ByteSpanTestTraits = SpanTestTraits<std::byte>;

#endif  // SRC_LIB_ZBITL_TESTS_SPAN_TESTS_H_
