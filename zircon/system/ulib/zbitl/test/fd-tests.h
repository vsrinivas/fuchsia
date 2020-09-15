// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_FD_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_FD_TESTS_H_

#include <fcntl.h>
#include <lib/zbitl/fd.h>

#include "tests.h"

struct FdTestTraits {
  using storage_type = fbl::unique_fd;
  using payload_type = off_t;

  static constexpr bool kDefaultConstructedViewHasStorageError = true;

  struct Context {
    storage_type TakeStorage() { return std::move(storage_); }

    storage_type storage_;
  };

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    ASSERT_TRUE(fd);
    context->storage_ = std::move(fd);
  }

  static void Read(const storage_type& storage, payload_type payload, size_t size,
                   Bytes* contents) {
    contents->resize(size);
    ssize_t n = pread(storage.get(), contents->data(), size, payload);
    ASSERT_GE(n, 0, "pread: %s", strerror(errno));
    ASSERT_EQ(size, static_cast<uint32_t>(n), "did not fully read payload");
  }
};

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_FD_TESTS_H_
