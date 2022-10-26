// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index.h"

#include <inttypes.h>
#include <time.h>

#include <ostream>
#include <sstream>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/dwarf_binary_impl.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/developer/debug/zxdb/symbols/symbol_factory.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"
#include "src/lib/fxl/strings/split_string.h"

namespace zxdb {

// Generates the symbol index of our simple test app. This may get updated if we change things
// but the important thing is that when this happens to check that the new index makes sense and
// then add it.
TEST(Index, IndexDump) {
  TestSymbolModule setup(TestSymbolModule::kCheckedIn);
  ASSERT_TRUE(setup.Init("", false).ok());

  Index index;
  index.CreateIndex(setup.symbols()->binary()->GetLLVMObjectFile());

  // Symbol index.
  std::ostringstream out;
  index.root().Dump(out, setup.symbols()->symbol_factory(), 0);
  const char kExpected[] = R"(  Namespaces:
    <<empty index string>>
      Functions:
        AnonNSFunction: {[0x14e0, 0x14ef)}
        LineLookupTest<0>: {[0x1040, 0x104f)}
        LineLookupTest<1>: {[0x1050, 0x105d)}
    my_ns
      Types:
        Base1: 0x1b3
        Base2: 0x1c3
        MyClass: 0x664
          Types:
            Inner: 0x683
              Functions:
                MyMemberTwo: {[0x1400, 0x140b)}
          Functions:
            MyMemberOne: {[0x14d0, 0x14df)}
          Variables:
            kClassStatic: 0x65b
        Struct: 0x154
          Functions:
            MyFunc: {[0x10f0, 0x1105)}
          Variables:
            kConstInt: 0x182
            kConstLongDouble: 0x18b
        StructMemberPtr: 0x287
        TypeForUsing: 0x1d3
      Functions:
        DoStructCall: {[0x1120, 0x116c)}
        GetStruct: {[0x10a0, 0x10c5)}
        GetStructMemberPtr: {[0x10d0, 0x10e1)}
        InlinedFunction: {}, {[0x12e0, 0x12e6)}
        NamespaceFunction: {[0x1410, 0x141b)}
        PassRValueRef: {[0x1110, 0x111a)}
      Variables:
        kGlobal: 0x64f
    std
      Types:
        nullptr_t: 0x363
  Types:
    ClassInTest2: 0x741
      Functions:
        FunctionInTest2: {[0x14f0, 0x14fb)}
    ForInline: 0x2b2
      Functions:
        ForInline: {[0x12b0, 0x12c4)}
        InlinedFunction: {[0x126d, 0x1276)}
    MyTemplate<my_ns::Struct, 42>: 0x124
      Functions:
        MyTemplate: {[0x11e0, 0x11f5)}
    StructWithEnums: 0xce
      Types:
        RegularEnum: 0xdd
        TypedEnum: 0x107
    VirtualBase: 0x319
      Functions:
        DoIt: {}
        VirtualBase: {[0x13e0, 0x13fc)}
    VirtualDerived: 0x2e3
      Functions:
        DoIt: {}
        VirtualDerived: {[0x13a0, 0x13df)}
    __ARRAY_SIZE_TYPE__: 0x5df
    char: 0x2ae
    int: 0x9a
    long double: 0x2a5
    signed char: 0x120
    unsigned int: 0x118
  Functions:
    CallInline: {[0x12d0, 0x12e8)}
    CallInlineMember: {[0x1200, 0x12a5)}
    DoLineLookupTest: {[0x1000, 0x1034)}
    GetIntPtr: {[0x1060, 0x1068)}
    GetNullPtrT: {[0x1320, 0x133f)}
    GetString: {[0x1070, 0x1099)}
    GetStructWithEnums: {[0x12f0, 0x131a)}
    GetTemplate: {[0x11a0, 0x11e0)}
    GetUsing: {[0x1340, 0x1359)}
    GetVirtualDerived: {[0x1360, 0x1399)}
    My2DArray: {[0x1170, 0x1193)}
    MyFunction: {[0x1420, 0x14cd)}
)";
  EXPECT_EQ(kExpected, out.str());

  // File index.
  std::ostringstream files;
  index.DumpFileIndex(files);
  const char kExpectedFiles[] =
      R"(line_lookup_symbol_test.cc -> ../../src/developer/debug/zxdb/symbols/test_data/line_lookup_symbol_test.cc -> 1 units
type_test.cc -> ../../src/developer/debug/zxdb/symbols/test_data/type_test.cc -> 1 units
zxdb_symbol_test.cc -> ../../src/developer/debug/zxdb/symbols/test_data/zxdb_symbol_test.cc -> 1 units
zxdb_symbol_test2.cc -> ../../src/developer/debug/zxdb/symbols/test_data/zxdb_symbol_test2.cc -> 1 units
)";
  EXPECT_EQ(kExpectedFiles, files.str());

  // Test that the slow indexing path produces the same result as the fast path.
  Index slow_index;
  slow_index.CreateIndex(setup.symbols()->binary()->GetLLVMObjectFile(), true);
  out = std::ostringstream();
  slow_index.root().Dump(out, setup.symbols()->symbol_factory(), 0);
  EXPECT_EQ(kExpected, out.str());
}

TEST(Index, FindExactFunction) {
  TestSymbolModule setup(TestSymbolModule::kCheckedIn);
  ASSERT_TRUE(setup.Init("", false).ok());

  Index index;
  index.CreateIndex(setup.symbols()->binary()->GetLLVMObjectFile());

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

TEST(Index, FindFileMatches) {
  TestSymbolModule setup(TestSymbolModule::kCheckedIn);
  ASSERT_TRUE(setup.Init("", false).ok());

  Index index;
  index.CreateIndex(setup.symbols()->binary()->GetLLVMObjectFile());

  // Simple filename-only query that succeeds.
  std::vector<std::string> result = index.FindFileMatches("zxdb_symbol_test.cc");
  ASSERT_EQ(1u, result.size());
  EXPECT_TRUE(StringEndsWith(result[0], "symbols/test_data/zxdb_symbol_test.cc"));

  // Save the full path for later.
  std::string full_path = result[0];

  // Simple filename-only query that fails.
  result = index.FindFileMatches("nonexistent.cc");
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

TEST(Index, FindFilePrefixes) {
  TestSymbolModule setup(TestSymbolModule::kCheckedIn);
  ASSERT_TRUE(setup.Init("", false).ok());

  Index index;
  index.CreateIndex(setup.symbols()->binary()->GetLLVMObjectFile());

  // Should find both files. Order not guaranteed.
  std::vector<std::string> result = index.FindFilePrefixes("z");
  ASSERT_EQ(2u, result.size());
  EXPECT_NE(result.end(), std::find(result.begin(), result.end(), "zxdb_symbol_test.cc"));
  EXPECT_NE(result.end(), std::find(result.begin(), result.end(), "zxdb_symbol_test2.cc"));
}

// Enable and substitute a path on your system to dump the index for a DWARF file.
#if 0
TEST(Index, DumpIndex) {
  TestSymbolModule setup("/path/to/symbol/file/goes.here", "test");
  ASSERT_TRUE(setup.Init("", false).ok());

  Index index;
  index.CreateIndex(setup.symbols()->binary()->GetLLVMObjectFile());

  std::cout << index.main_functions().size() << " main function(s) found.\n\n";

  std::cout << "Symbol index dump:\n";
  index.root().Dump(std::cout, setup.symbols()->symbol_factory(), 1);

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

TEST(Index, BenchmarkIndexing) {
  const char kFilename[] = "chrome";
  int64_t begin_us = GetTickMicroseconds();

  TestSymbolModule setup(kFileName, "");
  ASSERT_TRUE(setup.Init("", false).ok());

  int64_t load_complete_us = GetTickMicroseconds();

  Index index;
  index.CreateIndex(setup.symbols()->binary()->GetLLVMObjectFile());

  int64_t index_complete_us = GetTickMicroseconds();

  printf("\nIndexing results for %s:\n   Load: %" PRId64
         " µs\n  Index: %" PRId64 " µs\n\n",
         kFilename, load_complete_us - begin_us,
         index_complete_us - load_complete_us);

  sleep(10);
}
#endif  // End indexing benchmark.

}  // namespace zxdb
