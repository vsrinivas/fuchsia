// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/procfs/cpp/internal/environ.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <unistd.h>

namespace procfs {
namespace internal {

// TODO(abarth): What is a reasonable maximum size?
static constexpr size_t kMaxEnvironSize = 32 * 1024;

std::unique_ptr<vfs::PseudoFile> CreateEnviron() {
  return std::make_unique<vfs::PseudoFile>(
      kMaxEnvironSize, [](std::vector<uint8_t>* out, size_t max_bytes) {
        size_t bytes_remaining = max_bytes;
        for (char** env = environ; *env; ++env) {
          size_t len = strlen(*env) + 1;  // Include the null byte.
          if (len > bytes_remaining) {
            break;
          }
          std::copy(*env, *env + len, std::back_inserter(*out));
          bytes_remaining -= len;
        }
        return ZX_OK;
      });
}

}  // namespace internal
}  // namespace procfs
