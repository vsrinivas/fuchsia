// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_STDIO_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_STDIO_TESTS_H_

#include <lib/zbitl/stdio.h>

#include "src/lib/files/scoped_temp_dir.h"
#include "tests.h"

struct StdioTestTraits {
  using storage_type = FILE*;
  using payload_type = long int;

  struct Context {
    ~Context() {
      if (storage_) {
        fclose(storage_);
      }
    }

    storage_type TakeStorage() const { return storage_; }

    storage_type storage_ = nullptr;
    files::ScopedTempDir dir_;
  };

  static void Create(size_t size, Context* context) {
    std::string filename;
    ASSERT_TRUE(context->dir_.NewTempFile(&filename));
    FILE* f = fopen(filename.c_str(), "r+");
    ASSERT_NOT_NULL(f, "failed to open %s: %s", filename.c_str(), strerror(errno));
    ASSERT_GE(size, 1);
    fseek(f, static_cast<long int>(size) - 1, SEEK_SET);
    putc(0, f);
    context->storage_ = f;
    ASSERT_FALSE(ferror(f));
  }

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    ASSERT_TRUE(fd);
    FILE* f = fdopen(fd.release(), "r+");
    ASSERT_NOT_NULL(f, "failed to open descriptor: %s", strerror(errno));
    context->storage_ = f;
  }

  static void Read(storage_type storage, payload_type payload, size_t size, Bytes* contents) {
    contents->resize(size);
    ASSERT_EQ(0, fseek(storage, payload, SEEK_SET), "failed to seek to payload: %s",
              strerror(errno));
    size_t n = fread(contents->data(), 1, size, storage);
    ASSERT_EQ(0, ferror(storage), "failed to read payload: %s", strerror(errno));
    ASSERT_EQ(size, n, "did not fully read payload");
  }

  static payload_type AsPayload(storage_type storage) { return 0; }
};

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_STDIO_TESTS_H_
