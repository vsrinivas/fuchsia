// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_

#include <lib/zbitl/memory.h>

#include "tests.h"

template <typename T>
struct FblSpanTestTraits {
  using storage_type = fbl::Span<T>;
  using payload_type = storage_type;

  static constexpr bool kDefaultConstructedViewHasStorageError = false;
  static constexpr bool kExpectExtensibility = false;
  static constexpr bool kExpectOneshotReads = true;
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
                   Bytes* contents) {
    contents->resize(size);
    auto bytes = fbl::as_bytes(payload);
    ASSERT_LE(size, bytes.size());
    memcpy(contents->data(), bytes.data(), size);
  }

  static void Write(storage_type& storage, uint32_t offset, const Bytes& data) {
    ASSERT_LT(offset, storage.size());
    ASSERT_LE(offset, storage.size() - data.size());
    memcpy(storage.data() + offset, data.data(), data.size());
  }

  static void ToPayload(const storage_type& storage, uint32_t offset, payload_type& payload) {
    ASSERT_LT(offset, storage.size());
    payload = storage.subspan(offset);
  }
};

template <typename T>
struct FblArrayTestTraits {
  using storage_type = fbl::Array<T>;
  using payload_type = fbl::Span<T>;
  using creation_traits = FblArrayTestTraits;
  using SpanTraits = FblSpanTestTraits<T>;

  static constexpr bool kDefaultConstructedViewHasStorageError = false;
  static constexpr bool kExpectExtensibility = true;
  static constexpr bool kExpectOneshotReads = true;
  static constexpr bool kExpectUnbufferedReads = true;
  static constexpr bool kExpectUnbufferedWrites = !std::is_const_v<T>;

  struct Context {
    storage_type TakeStorage() { return std::move(storage_); }

    storage_type storage_;
  };

  static void Create(size_t size, Context* context) {
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    storage_type storage{new T[n], n};
    *context = {std::move(storage)};
  }

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    ASSERT_TRUE(fd);
    ASSERT_NO_FATAL_FAILURE(Create(size, context));
    EXPECT_EQ(static_cast<ssize_t>(size), read(fd.get(), context->storage_.data(), size));
  }

  static void Read(const storage_type& storage, payload_type payload, size_t size,
                   Bytes* contents) {
    fbl::Span<T> span{storage.data(), storage.size()};
    ASSERT_NO_FATAL_FAILURE(SpanTraits::Read(span, payload, size, contents));
  }

  static void Write(storage_type& storage, uint32_t offset, const Bytes& data) {
    fbl::Span<T> span{storage.data(), storage.size()};
    ASSERT_NO_FATAL_FAILURE(SpanTraits::Write(span, offset, data));
  }

  static void ToPayload(const storage_type& storage, uint32_t offset, payload_type& payload) {
    ASSERT_LT(offset, storage.size());
    payload = payload_type{storage.data() + offset, storage.size() - offset};
  }
};

using FblByteArrayTestTraits = FblArrayTestTraits<std::byte>;

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_
