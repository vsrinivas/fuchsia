// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/run_test_component.h"

#include <string>

#include "gtest/gtest.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"

namespace run {
namespace {

TEST(Url, ParseURL) {
  EXPECT_EQ("", GetComponentManifestPath(""));
  EXPECT_EQ("", GetComponentManifestPath("random_string"));
  EXPECT_EQ("", GetComponentManifestPath("https://google.com"));
  EXPECT_EQ("", GetComponentManifestPath(
                    "fuchsia-pkg://fuchsia.com/component_hello_world#"));

  EXPECT_EQ(
      "/pkgfs/packages/component_hello_world/0/meta/hello.cmx",
      GetComponentManifestPath(
          "fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx"));
}

TEST(Url, GenerateComponentUrl) {
  EXPECT_EQ("", GenerateComponentUrl(""));

  EXPECT_EQ("", GenerateComponentUrl("/system/sys/pname/0/meta/hello.cmx"));

  EXPECT_EQ("", GenerateComponentUrl("pname"));

  EXPECT_EQ("", GenerateComponentUrl("pname/0/meta/foo"));

  EXPECT_EQ("", GenerateComponentUrl("pname/meta/foo.cmx"));

  EXPECT_EQ("fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx",
            GenerateComponentUrl("component_hello_world/0/meta/hello.cmx"));
}

bool CreateEmptyFile(const std::string& path) {
  return files::WriteFile(path, "", 0);
}

TEST(RunTest, ParseArgs) {
  constexpr char kBinName[] = "bin_name";

  constexpr char component_url[] =
      "fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx";
  {
    const char* argv[] = {kBinName, component_url};
    auto result = ParseArgs(2, argv, "");
    EXPECT_FALSE(result.error);
    EXPECT_EQ(component_url, result.launch_info.url);
    EXPECT_EQ(0u, result.launch_info.arguments->size());
    EXPECT_EQ(0u, result.matching_urls.size());
  }

  {
    const char* argv[] = {kBinName, component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(4, argv, "");
    EXPECT_FALSE(result.error);
    EXPECT_EQ(component_url, result.launch_info.url);
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[2], result.launch_info.arguments.get()[0]);
    EXPECT_EQ(argv[3], result.launch_info.arguments.get()[1]);
  }

  // Create filesystem to run glob on.
  files::ScopedTempDir dir;
  constexpr char test_pkg[] = "test_pkg";
  constexpr char test_file_prefix[] = "test_file";
  auto meta_dir_path = fxl::Substitute("$0/$1/0/meta", dir.path(), test_pkg);
  ASSERT_TRUE(files::CreateDirectory(meta_dir_path))
      << meta_dir_path << " " << errno;
  auto cmx_file_path1 =
      fxl::Substitute("$0/$11.cmx", meta_dir_path, test_file_prefix);
  auto cmx_file_path2 =
      fxl::Substitute("$0/$12.cmx", meta_dir_path, test_file_prefix);
  auto cmx_file_path3 =
      fxl::Substitute("$0/$13.cmx", meta_dir_path, test_file_prefix);
  ASSERT_TRUE(CreateEmptyFile(cmx_file_path1));
  ASSERT_TRUE(CreateEmptyFile(cmx_file_path2));
  ASSERT_TRUE(CreateEmptyFile(cmx_file_path3));
  auto expected_url1 = fxl::StringPrintf(
      "fuchsia-pkg://fuchsia.com/%s#meta/%s1.cmx", test_pkg, test_file_prefix);
  auto expected_url2 = fxl::StringPrintf(
      "fuchsia-pkg://fuchsia.com/%s#meta/%s2.cmx", test_pkg, test_file_prefix);
  auto expected_url3 = fxl::StringPrintf(
      "fuchsia-pkg://fuchsia.com/%s#meta/%s3.cmx", test_pkg, test_file_prefix);

  {
    const char* argv[] = {kBinName, "test_file*"};
    auto result = ParseArgs(2, argv, dir.path());
    EXPECT_TRUE(result.error);
  }

  {
    const char* argv[] = {kBinName, "test_file"};
    auto result = ParseArgs(2, argv, dir.path());
    EXPECT_FALSE(result.error);
    ASSERT_EQ(3u, result.matching_urls.size());
    EXPECT_EQ(result.matching_urls[0], expected_url1);
    EXPECT_EQ(result.matching_urls[1], expected_url2);
    EXPECT_EQ(result.matching_urls[2], expected_url3);
    EXPECT_EQ(result.cmx_file_path, cmx_file_path1);
  }

  {
    const char* argv[] = {kBinName, "test_file2"};
    auto result = ParseArgs(2, argv, dir.path());
    EXPECT_FALSE(result.error);
    ASSERT_EQ(1u, result.matching_urls.size());
    EXPECT_EQ(result.matching_urls[0], expected_url2);
    EXPECT_EQ(expected_url2, result.launch_info.url);
    EXPECT_EQ(result.cmx_file_path, cmx_file_path2);
  }
}

}  // namespace
}  // namespace run
