// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_

#include <lib/zbitl/memory.h>

#include "tests.h"
#include "view-tests.h"

template <typename T>
struct FblArrayTestTraits {
  using storage_type = fbl::Array<T>;
  using payload_type = cpp20::span<T>;
  using creation_traits = FblArrayTestTraits;
  using SpanTraits = SpanTestTraits<T>;

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
    auto span = zbitl::AsSpan(storage);
    ASSERT_NO_FATAL_FAILURE(SpanTraits::Read(span, payload, size, contents));
  }

  static void Write(storage_type& storage, uint32_t offset, const Bytes& data) {
    auto span = zbitl::AsSpan(storage);
    ASSERT_NO_FATAL_FAILURE(SpanTraits::Write(span, offset, data));
  }

  static void ToPayload(const storage_type& storage, uint32_t offset, payload_type& payload) {
    ASSERT_LT(offset, storage.size());
    payload = payload_type{storage.data() + offset, storage.size() - offset};
  }
};

using FblByteArrayTestTraits = FblArrayTestTraits<std::byte>;

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_
