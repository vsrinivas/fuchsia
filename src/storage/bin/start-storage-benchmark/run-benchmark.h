// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BIN_STORAGE_BENCHMARK_RUN_BENCHMARK_
#define SRC_STORAGE_BIN_STORAGE_BENCHMARK_RUN_BENCHMARK_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/status.h>

#include <string>
#include <vector>

namespace storage_benchmark {

// Runs the component |component_url| with the provided |args|. |filesystem| is added to the
// component's namespace at |mount_point|. Returns an error if the component failed to start or
// stopped with an exit code other than zero.
zx::status<> RunBenchmark(const std::string& component_url, const std::vector<std::string>& args,
                          fidl::ClientEnd<fuchsia_io::Directory> filesystem,
                          const std::string& mount_point);

}  // namespace storage_benchmark

#endif  // SRC_STORAGE_BIN_STORAGE_BENCHMARK_RUN_BENCHMARK_
