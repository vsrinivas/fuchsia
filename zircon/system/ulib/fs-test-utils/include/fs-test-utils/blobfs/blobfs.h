// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/io.h>

namespace fs_test_utils {

using BlobSrcFunction = void (*)(char* data, size_t length);

// An in-memory representation of a blob.
struct BlobInfo {
  char path[PATH_MAX];
  fbl::unique_ptr<char[]> merkle;
  size_t size_merkle;
  fbl::unique_ptr<char[]> data;
  size_t size_data;
};

template <typename T, typename U>
int StreamAll(T func, int fd, U* buf, size_t max) {
  size_t n = 0;
  while (n != max) {
    ssize_t d = func(fd, &buf[n], max - n);
    if (d < 0) {
      return -1;
    }
    n += d;
  }
  return 0;
}

void RandomFill(char* data, size_t length);

bool GenerateBlob(BlobSrcFunction sourceCb, fbl::String mount_path, size_t size_data,
                  fbl::unique_ptr<BlobInfo>* out);

bool GenerateRandomBlob(fbl::String mount_path, size_t size_data, fbl::unique_ptr<BlobInfo>* out);

bool VerifyContents(int fd, const char* data, size_t size_data);

}  // namespace fs_test_utils
