// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_

#include <lib/zbitl/memory.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

template <typename T>
struct FblArrayTestTraits {
  using storage_type = fbl::Array<T>;

  static constexpr bool kDefaultConstructedViewHasStorageError = false;

  struct Context {
    storage_type TakeStorage() { return std::move(storage_); }

    fbl::Array<T> storage_;
  };

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    ASSERT_TRUE(fd);
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    fbl::Array<T> storage{new T[n], n};
    ASSERT_EQ(size, read(fd.get(), storage.data(), size));
    *context = {std::move(storage)};
  }

  static void Read(const fbl::Array<T>& storage, fbl::Span<T> payload, size_t size,
                   std::string* contents) {
    contents->resize(size);
    auto bytes = fbl::as_bytes(payload);
    ASSERT_EQ(size, bytes.size());
    memcpy(contents->data(), bytes.data(), bytes.size());
  }
};

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_
