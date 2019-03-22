// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/elf.h"
#include "gtest/gtest.h"

#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace debug_ipc {

#if !defined(__Fuchsia__)
// This test requires a test data file which has not been set up for packaging
// on Fuchsia yet.
// TODO(brettw) set this up and enable this test on Fuchsia.

namespace {

std::string GetSelfPath() {
  std::string result;
#if defined(__APPLE__)
  // Executable path can have relative references ("..") depending on how the
  // app was launched.
  uint32_t length = 0;
  _NSGetExecutablePath(nullptr, &length);
  result.resize(length);
  _NSGetExecutablePath(&result[0], &length);
  result.resize(length - 1);  // Length included terminator.
#elif defined(__linux__)
  // The realpath() call below will resolve the symbolic link.
  result.assign("/proc/self/exe");
#else
#error Write this for your platform.
#endif

  char fullpath[PATH_MAX];
  return std::string(realpath(result.c_str(), fullpath));
}

std::string GetSmallTestElfFileName() {
  std::string path = GetSelfPath();
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    path = "./";  // Just hope the current directory works.
  } else {
    path.resize(last_slash + 1);
  }
  path += "test_data/debug_ipc/small_test_file.elf";
  return path;
}

}  // namespace

TEST(Elf, ExtractBuildID) {
  std::string small_test_file_name = GetSmallTestElfFileName();

  FILE* small_test_file = fopen(small_test_file_name.c_str(), "rb");
  ASSERT_TRUE(small_test_file) << small_test_file_name.c_str();
  std::string build_id = ExtractBuildID(small_test_file);
  fclose(small_test_file);

  // This expected build ID was extracted with:
  //   eu-readelf -n small_test_file.elf
  EXPECT_EQ("763feb38b0e37a89964c330c5cf7f7af2ce79e54", build_id);
}
#endif

}  // namespace debug_ipc
