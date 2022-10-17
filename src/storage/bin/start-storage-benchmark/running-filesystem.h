// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BIN_STORAGE_BENCHMARK_RUNNING_FILESYSTEM_
#define SRC_STORAGE_BIN_STORAGE_BENCHMARK_RUNNING_FILESYSTEM_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/status.h>

namespace storage_benchmark {

// Interface for a running filesystem.
class RunningFilesystem {
 public:
  virtual ~RunningFilesystem() = default;

  // Returns a channel to the filesystem's root directory.
  virtual zx::result<fidl::ClientEnd<fuchsia_io::Directory>> GetFilesystemRoot() const = 0;
};

}  // namespace storage_benchmark

#endif  // SRC_STORAGE_BIN_STORAGE_BENCHMARK_RUNNING_FILESYSTEM_
