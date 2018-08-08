// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <time.h>
#include <ostream>

#include "garnet/bin/zxdb/client/symbols/module_symbol_index.h"
#include "garnet/bin/zxdb/client/symbols/test_symbol_module.h"
#include "garnet/bin/zxdb/common/string_util.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(ModuleSymbolIndex, FindFunctionExact) {
  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.Load(&err)) << err;

  ModuleSymbolIndex index;
  index.CreateIndex(module.object_file());

#if 0
  // Enable to dump the found index for debugging purposes.
  std::cout << "Index dump:\n";
  index.root().Dump(std::cout, 1);
  index.DumpFileIndex(std::cout);
#endif

  // Standalone function search.
  auto result = index.FindFunctionExact(TestSymbolModule::kMyFunctionName);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";

  // Standalone function inside a namespace.
  result = index.FindFunctionExact(TestSymbolModule::kNamespaceFunctionName);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";

  // Namespace + class member function search.
  result = index.FindFunctionExact(TestSymbolModule::kMyMemberOneName);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";

  // Same but in the 2nd compilation unit (tests unit-relative addressing).
  result = index.FindFunctionExact(TestSymbolModule::kFunctionInTest2Name);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";

  // Namespace + class + struct with static member function search.
  result = index.FindFunctionExact(TestSymbolModule::kMyMemberTwoName);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";
}

TEST(ModuleSymbolIndex, FindFileMatches) {
  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.Load(&err)) << err;

  ModuleSymbolIndex index;
  index.CreateIndex(module.object_file());

  // Simple filename-only query that succeeds.
  std::vector<std::string> result =
      index.FindFileMatches("zxdb_symbol_test.cc");
  ASSERT_EQ(1u, result.size());
  EXPECT_TRUE(
      StringEndsWith(result[0], "client/test_data/zxdb_symbol_test.cc"));

  // Save the full path for later.
  std::string full_path = result[0];

  // Simple filename-only query that fails.
  result = index.FindFileMatches("nonexistant.cc");
  EXPECT_EQ(0u, result.size());

  // Multiple path components.
  result = index.FindFileMatches("client/test_data/zxdb_symbol_test.cc");
  EXPECT_EQ(1u, result.size());

  // Ends-with match but doesn't start on a slash boundary.
  result = index.FindFileMatches("nt/test_data/zxdb_symbol_test.cc");
  EXPECT_EQ(0u, result.size());

  // Full path match.
  result = index.FindFileMatches(full_path);
  EXPECT_EQ(1u, result.size());

  // More-than-full path match.
  result = index.FindFileMatches("/a" + full_path);
  EXPECT_EQ(0u, result.size());
}

// Enable and substitute a path on your system for kFilename to run the
// indexing benchmark.
#if 0
static int64_t GetTickMicroseconds() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  constexpr int64_t kMicrosecondsPerSecond = 1000000;
  constexpr int64_t kNanosecondsPerMicrosecond = 1000;

  int64_t result = ts.tv_sec * kMicrosecondsPerSecond;
  result += (ts.tv_nsec / kNanosecondsPerMicrosecond);
  return result;
}

TEST(ModuleSymbolIndex, BenchmarkIndexing) {
  const char kFilename[] =
      "/usr/local/google/home/brettw/prj/src/out/release/chrome";
  int64_t begin_us = GetTickMicroseconds();

  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.LoadSpecific(kFilename, &err)) << err;

  int64_t load_complete_us = GetTickMicroseconds();

  ModuleSymbolIndex index;
  index.CreateIndex(module.object_file());

  int64_t index_complete_us = GetTickMicroseconds();

  printf("\nIndexing results for %s:\n   Load: %" PRId64
         " µs\n  Index: %" PRId64 " µs\n\n",
         kFilename, load_complete_us - begin_us,
         index_complete_us - load_complete_us);

  sleep(10);
}
#endif  // End indexing benchmark.

}  // namespace zxdb
