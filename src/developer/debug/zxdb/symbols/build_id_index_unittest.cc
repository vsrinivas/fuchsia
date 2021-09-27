// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/build_id_index.h"

#include <filesystem>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/host_util.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"

namespace zxdb {

namespace {

const char kSmallTestBuildID[] = "763feb38b0e37a89964c330c5cf7f7af2ce79e54";

std::filesystem::path GetTestDataDir() {
  return std::filesystem::path(GetSelfPath()).parent_path() / "test_data/zxdb";
}

std::filesystem::path GetSmallTestFile() { return GetTestDataDir() / "small_test_file.elf"; }
std::filesystem::path GetSymbolTestSoBuildIDPath() {
  // Construct the expected name, using the first two build id chars as a subdirectory.
  std::string build_id(TestSymbolModule::kCheckedInBuildId);
  return GetTestDataDir() / "build_id/.build-id" / build_id.substr(0, 2) /
         (build_id.substr(2) + ".debug");
}

}  // namespace

// Index one individual file.
TEST(BuildIDIndex, IndexFile) {
  BuildIDIndex index;
  std::string test_file = GetSmallTestFile();
  index.AddPlainFileOrDir(test_file);

  // The known file should be found. We have no debug symbols for this binary,
  // so it shouldn't show as debug info.
  EXPECT_EQ("", index.EntryForBuildID(kSmallTestBuildID).debug_info);
  EXPECT_EQ(test_file, index.EntryForBuildID(kSmallTestBuildID).binary);

  // Test some random build ID fails.
  EXPECT_EQ("", index.EntryForBuildID("random build id").debug_info);
}

// Index all files in a directory.
TEST(BuildIDIndex, IndexDir) {
  BuildIDIndex index;
  index.AddPlainFileOrDir(GetTestDataDir());

  // It should have found the small test file and indexed it.
  EXPECT_EQ(GetSmallTestFile(), index.EntryForBuildID(kSmallTestBuildID).binary);
}

// Index all files in a specifically-named build ID folder
TEST(BuildIDIndex, IndexBuildIdDir) {
  BuildIDIndex index;
  index.AddBuildIdDir(GetTestDataDir() / "build_id/.build-id");

  // We should be able to look up the test file.
  EXPECT_EQ(GetSymbolTestSoBuildIDPath(),
            index.EntryForBuildID(TestSymbolModule::kCheckedInBuildId).binary);
  EXPECT_EQ(GetSymbolTestSoBuildIDPath(),
            index.EntryForBuildID(TestSymbolModule::kCheckedInBuildId).debug_info);
}

TEST(BuildIDIndex, ReadFromSymbolIndexPlain) {
  BuildIDIndex index;
  index.AddSymbolIndexFile(GetTestDataDir() / "symbol-index");

  EXPECT_EQ(1ul, index.build_id_dirs().size());
  EXPECT_EQ(0ul, index.ids_txts().size());
}

TEST(BuildIDIndex, ReadFromSymbolIndexJSON) {
  BuildIDIndex index;
  index.AddSymbolIndexFile(GetTestDataDir() / "symbol-index.json");

  EXPECT_EQ(2ul, index.build_id_dirs().size());
  EXPECT_EQ(GetTestDataDir().parent_path(), index.build_id_dirs()[0].path);
  EXPECT_EQ(GetTestDataDir().parent_path() / "build", index.build_id_dirs()[0].build_dir);
  EXPECT_EQ("/", index.build_id_dirs()[1].path);
  EXPECT_EQ("", index.build_id_dirs()[1].build_dir);
  EXPECT_EQ(2ul, index.symbol_servers().size());
  EXPECT_EQ("gs://bucket", index.symbol_servers()[0].url);
  EXPECT_EQ(false, index.symbol_servers()[0].require_authentication);
  EXPECT_EQ("gs://another-bucket", index.symbol_servers()[1].url);
  EXPECT_EQ(true, index.symbol_servers()[1].require_authentication);
}

TEST(BuildIDIndex, ParseIDFile) {
  // Malformed line (no space) and empty line should be ignored. First one also
  // has two spaces separating which should be handled.
  const char test_data[] =
      R"(ff344c5304043feb  /home/me/fuchsia/out/x64/exe.unstripped/false
ff3a9a920026380f8990a27333ed7634b3db89b9 /home/me/fuchsia/out/build-zircon/build-x64/system/dev/display/imx8m-display/libimx8m-display.so
asdf

ffc2990b78544c1cee5092c3bf040b53f2af10cf /home/me/fuchsia/out/build-zircon/build-x64/system/uapp/channel-perf/channel-perf.elf
deadb33fbadf00dbaddadbabb relative/path/dummy.elf
)";

  BuildIDIndex::BuildIDMap map;
  BuildIDIndex::ParseIDs(test_data, GetTestDataDir(), "", &map);

  EXPECT_EQ(4u, map.size());
  EXPECT_EQ("/home/me/fuchsia/out/x64/exe.unstripped/false", map["ff344c5304043feb"].debug_info);
  EXPECT_EQ(
      "/home/me/fuchsia/out/build-zircon/build-x64/system/dev/display/"
      "imx8m-display/libimx8m-display.so",
      map["ff3a9a920026380f8990a27333ed7634b3db89b9"].debug_info);
  EXPECT_EQ(
      "/home/me/fuchsia/out/build-zircon/build-x64/system/uapp/channel-perf/"
      "channel-perf.elf",
      map["ffc2990b78544c1cee5092c3bf040b53f2af10cf"].debug_info);
  EXPECT_EQ(GetTestDataDir() / "relative/path/dummy.elf",
            map["deadb33fbadf00dbaddadbabb"].debug_info);
}

}  // namespace zxdb
