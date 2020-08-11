// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/build_id_index.h"

#include <filesystem>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/host_util.h"

namespace zxdb {

namespace {

const char kSmallTestBuildID[] = "763feb38b0e37a89964c330c5cf7f7af2ce79e54";
const char kSymbolTestSoBuildID[] = "596f4c8afa5a0a43";

std::filesystem::path GetTestDataDir() {
  std::filesystem::path path(GetSelfPath());
  path.remove_filename();
  path.append("test_data/zxdb/");
  return path;
}

std::filesystem::path GetSmallTestFile() { return GetTestDataDir() / "small_test_file.elf"; }
std::filesystem::path GetSymbolTestSoBuildIDPath() {
  return GetTestDataDir() / "build_id/.build-id/59/6f4c8afa5a0a43.debug";
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
  EXPECT_EQ(GetSymbolTestSoBuildIDPath(), index.EntryForBuildID(kSymbolTestSoBuildID).binary);
  EXPECT_EQ(GetSymbolTestSoBuildIDPath(), index.EntryForBuildID(kSymbolTestSoBuildID).debug_info);
}

TEST(BuildIDIndex, ReadFromSymbolIndex) {
  BuildIDIndex index;
  index.AddSymbolIndexFile(GetTestDataDir() / "symbol-index");

  // It's a little bit tricky because we actually want to check that
  //   EXPECT_EQ(1, index.build_id_dirs_.size());
  //   EXPECT_EQ(0, index.ids_txts_.size());
  // but both of them are private.
  EXPECT_EQ(1UL, index.GetStatus().size());
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
