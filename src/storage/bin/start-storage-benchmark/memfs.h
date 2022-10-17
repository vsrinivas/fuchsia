// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BIN_STORAGE_BENCHMARK_MEMFS_
#define SRC_STORAGE_BIN_STORAGE_BENCHMARK_MEMFS_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/status.h>

#include <memory>

#include "src/storage/bin/start-storage-benchmark/running-filesystem.h"
#include "src/storage/memfs/scoped_memfs.h"

namespace storage_benchmark {

// Wrapper around a |memfs::Setup| instance that conforms to the |RunningFilesystem| interface.
class Memfs : public RunningFilesystem {
 public:
  // Starts a memfs instance in a new thread.
  static zx::result<std::unique_ptr<Memfs>> Create();

  zx::result<fidl::ClientEnd<fuchsia_io::Directory>> GetFilesystemRoot() const override;

 private:
  explicit Memfs(std::unique_ptr<async::Loop> loop, ScopedMemfs memfs)
      : loop_(std::move(loop)), memfs_(std::move(memfs)) {}

  std::unique_ptr<async::Loop> loop_;
  ScopedMemfs memfs_;
};

}  // namespace storage_benchmark

#endif  // SRC_STORAGE_BIN_STORAGE_BENCHMARK_MEMFS_
