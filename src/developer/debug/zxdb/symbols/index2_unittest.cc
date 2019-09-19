// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index2.h"

#include <inttypes.h>
#include <time.h>

#include <ostream>
#include <sstream>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"
#include "src/lib/fxl/strings/split_string.h"

namespace zxdb {

// Generates the symbol index of our simple test app. This may get updated if we change things
// but the important thing is that when this happens to check that the new index makes sense and
// then add it.
TEST(Index2, IndexDump) {
  TestSymbolModule module;

  std::string err;
  ASSERT_TRUE(module.LoadSpecific(TestSymbolModule::GetCheckedInTestFileName(), &err)) << err;

  Index2 index;
  index.CreateIndex(module.object_file());

  // Symbol index.
  std::ostringstream out;
  index.root().Dump(out, 0);
  const char kExpected[] = R"(  Namespaces:
    <<empty index string>>
      Functions:
        LineLookupTest<0>
        LineLookupTest<1>
    my_ns
      Types:
        MyClass
          Types:
            Inner
              Functions:
                MyMemberTwo
          Functions:
            MyMemberOne
          Variables:
            kClassStatic
      Functions:
        NamespaceFunction
      Variables:
        kGlobal
  Types:
    ClassInTest2
      Functions:
        FunctionInTest2
    int
  Functions:
    DoLineLookupTest
    MyFunction
)";
  EXPECT_EQ(kExpected, out.str());

  // File index.
  std::ostringstream files;
  index.DumpFileIndex(files);
  const char kExpectedFiles[] =
      R"(line_lookup_symbol_test.cc -> ../../garnet/bin/zxdb/symbols/test_data/line_lookup_symbol_test.cc -> 1 units
zxdb_symbol_test.cc -> ../../garnet/bin/zxdb/symbols/test_data/zxdb_symbol_test.cc -> 1 units
zxdb_symbol_test2.cc -> ../../garnet/bin/zxdb/symbols/test_data/zxdb_symbol_test2.cc -> 1 units
)";
  EXPECT_EQ(kExpectedFiles, files.str());
}

TEST(Index2, FindExactFunction) {
  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.Load(&err)) << err;

  Index2 index;
  index.CreateIndex(module.object_file());

  // Standalone function search.
  auto result = index.FindExact(TestSymbolModule::SplitName(TestSymbolModule::kMyFunctionName));
  EXPECT_EQ(1u, result.size()) << "Symbol not found: " << TestSymbolModule::kMyFunctionName;

  // Standalone function inside a named namespace.
  result = index.FindExact(TestSymbolModule::SplitName(TestSymbolModule::kNamespaceFunctionName));
  EXPECT_EQ(1u, result.size()) << "Symbol not found: " << TestSymbolModule::kNamespaceFunctionName;

  // Standalone function inside an anonymous namespace. Currently this is indexed as if the
  // anonymous namespace wasn't there, but this may need to change in the future.
  result = index.FindExact(TestSymbolModule::SplitName(TestSymbolModule::kAnonNSFunctionName));
  EXPECT_EQ(1u, result.size()) << "Symbol not found: " << TestSymbolModule::kAnonNSFunctionName;

  // Namespace + class member function search.
  result = index.FindExact(TestSymbolModule::SplitName(TestSymbolModule::kMyMemberOneName));
  EXPECT_EQ(1u, result.size()) << "Symbol not found: " << TestSymbolModule::kMyMemberOneName;

  // Same but in the 2nd compilation unit (tests unit-relative addressing).
  result = index.FindExact(TestSymbolModule::SplitName(TestSymbolModule::kFunctionInTest2Name));
  EXPECT_EQ(1u, result.size()) << "Symbol not found: " << TestSymbolModule::kFunctionInTest2Name;

  // Namespace + class + struct with static member function search.
  result = index.FindExact(TestSymbolModule::SplitName(TestSymbolModule::kMyMemberTwoName));
  EXPECT_EQ(1u, result.size()) << "Symbol not found: " << TestSymbolModule::kMyMemberTwoName;

  // Global variable.
  result = index.FindExact(TestSymbolModule::SplitName(TestSymbolModule::kGlobalName));
  EXPECT_EQ(1u, result.size()) << "Symbol not found: " << TestSymbolModule::kGlobalName;

  // Class static variable.
  result = index.FindExact(TestSymbolModule::SplitName(TestSymbolModule::kClassStaticName));
  EXPECT_EQ(1u, result.size()) << "Symbol not found: " << TestSymbolModule::kClassStaticName;

  // Something not found.
  result = index.FindExact(TestSymbolModule::SplitName("my_ns::MyClass::NotFoundThing"));
  EXPECT_TRUE(result.empty());
}

TEST(Index2, FindFileMatches) {
  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.Load(&err)) << err;

  Index2 index;
  index.CreateIndex(module.object_file());

  // Simple filename-only query that succeeds.
  std::vector<std::string> result = index.FindFileMatches("zxdb_symbol_test.cc");
  ASSERT_EQ(1u, result.size());
  EXPECT_TRUE(StringEndsWith(result[0], "symbols/test_data/zxdb_symbol_test.cc"));

  // Save the full path for later.
  std::string full_path = result[0];

  // Simple filename-only query that fails.
  result = index.FindFileMatches("nonexistant.cc");
  EXPECT_EQ(0u, result.size());

  // Multiple path components.
  result = index.FindFileMatches("symbols/test_data/zxdb_symbol_test.cc");
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

TEST(Index2, FindFilePrefixes) {
  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.Load(&err)) << err;

  Index2 index;
  index.CreateIndex(module.object_file());

  // Should find both files. Order not guaranteed.
  std::vector<std::string> result = index.FindFilePrefixes("z");
  ASSERT_EQ(2u, result.size());
  EXPECT_NE(result.end(), std::find(result.begin(), result.end(), "zxdb_symbol_test.cc"));
  EXPECT_NE(result.end(), std::find(result.begin(), result.end(), "zxdb_symbol_test2.cc"));
}

// Enable and substitute a path on your system to dump the index for a DWARF file.
#if 0
TEST(Index2, DumpIndex) {
  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.LoadSpecific("chrome", &err)) << err;

  Index2 index;
  index.CreateIndex(module.object_file());

  std::cout << index.main_functions().size() << " main function(s) found.\n\n";

  std::cout << "Symbol index dump:\n";
  index.root().Dump(std::cout, 1);

  std::cout << "File index dump:\n";
  index.DumpFileIndex(std::cout);
}
#endif

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

TEST(Index2, BenchmarkIndexing) {
  const char kFilename[] = "chrome";
  int64_t begin_us = GetTickMicroseconds();

  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.LoadSpecific(kFilename, &err)) << err;

  int64_t load_complete_us = GetTickMicroseconds();

  Index2 index;
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
