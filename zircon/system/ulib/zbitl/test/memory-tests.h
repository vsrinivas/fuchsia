// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_

#include <lib/zbitl/memory.h>

#include "tests.h"

template <typename T>
struct FblArrayTestTraits {
  using storage_type = fbl::Array<T>;
  using payload_type = fbl::Span<T>;
  using creation_traits = FblArrayTestTraits;

  static constexpr bool kDefaultConstructedViewHasStorageError = false;

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
    ASSERT_NO_FATAL_FAILURES(Create(size, context));
    ASSERT_EQ(size, read(fd.get(), context->storage_.data(), size));
  }

  static void Read(const storage_type& storage, payload_type payload, size_t size,
                   Bytes* contents) {
    contents->resize(size);
    auto bytes = fbl::as_bytes(payload);
    ASSERT_LE(size, bytes.size());
    memcpy(contents->data(), bytes.data(), size);
  }

  static payload_type AsPayload(const storage_type& storage) {
    return {storage.data(), storage.size()};
  }
};

using FblByteArrayTestTraits = FblArrayTestTraits<std::byte>;

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_MEMORY_TESTS_H_
