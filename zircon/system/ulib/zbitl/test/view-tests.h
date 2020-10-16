// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_VIEW_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_VIEW_TESTS_H_

#include <memory>
#include <string_view>

#include "tests.h"

template <typename T>
struct BasicStringViewTestTraits {
  using storage_type = std::basic_string_view<T>;
  using payload_type = std::basic_string_view<T>;

  static constexpr bool kDefaultConstructedViewHasStorageError = false;
  static constexpr bool kExpectOneshotReads = true;

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
    ASSERT_EQ(size, read(fd.get(), buff.get(), n));
    *context = {std::move(buff), n};
  }

  static void Read(storage_type storage, payload_type payload, size_t size, Bytes* contents) {
    *contents = {reinterpret_cast<const char*>(payload.data()), payload.size() * sizeof(T)};
  }
};

using StringTestTraits = BasicStringViewTestTraits<char>;
using ByteViewTestTraits = BasicStringViewTestTraits<std::byte>;

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_VIEW_TESTS_H_
