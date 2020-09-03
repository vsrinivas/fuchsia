// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_STDIO_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_STDIO_TESTS_H_

#include <lib/zbitl/stdio.h>

#include <string>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

struct StdioTestTraits {
  using storage_type = FILE*;

  struct Context {
    ~Context() {
      if (storage_) {
        fclose(storage_);
      }
    }

    storage_type TakeStorage() const { return storage_; }

    FILE* storage_ = nullptr;
  };

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    ASSERT_TRUE(fd);
    FILE* f = fdopen(fd.release(), "r+");
    ASSERT_NOT_NULL(f, "failed to open descriptor: %s", strerror(errno));
    context->storage_ = f;
  }

  static void Read(FILE* storage, long int payload, size_t size, std::string* contents) {
    contents->resize(size);
    ASSERT_EQ(0, fseek(storage, payload, SEEK_SET), "failed to seek to payload: %s",
              strerror(errno));
    size_t n = fread(contents->data(), 1, size, storage);
    ASSERT_EQ(0, ferror(storage), "failed to read payload: %s", strerror(errno));
    ASSERT_EQ(size, n, "did not fully read payload");
  }
};

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_STDIO_TESTS_H_
