// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/run-benchmark.h"

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"
#include "src/storage/bin/start-storage-benchmark/memfs.h"

namespace storage_benchmark {
namespace {

constexpr char kNamespaceValidatorComponentUrl[] =
    "fuchsia-pkg://fuchsia.com/start-storage-benchmark-namespace-validator#meta/"
    "start-storage-benchmark-namespace-validator.cmx";
constexpr char kMountPoint[] = "/benchmark";

TEST(RunBenchmarkTest, RunBenchmarkWillCorrectlyLaunchTheBenchmarkComponent) {
  std::vector<std::string> args = {kMountPoint};
  auto memfs = Memfs::Create();
  ASSERT_OK(memfs.status_value());
  auto root = memfs->GetFilesystemRoot();
  ASSERT_OK(root.status_value());

  auto result =
      RunBenchmark(kNamespaceValidatorComponentUrl, args, std::move(root).value(), kMountPoint);
  ASSERT_OK(result.status_value());
}

}  // namespace
}  // namespace storage_benchmark
