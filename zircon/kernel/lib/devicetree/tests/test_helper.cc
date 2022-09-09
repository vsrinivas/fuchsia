// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "test_helper.h"

#include <lib/stdcompat/span.h>

#include <string_view>

#ifndef __Fuchsia__
#include <libgen.h>
#include <unistd.h>
#endif  // !__Fuchsia__

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <zxtest/zxtest.h>

#if defined(__Fuchsia__)
constexpr std::string_view kTestDataDir = "/pkg/data";
#else
constexpr std::string_view kTestDataDir = "test_data/devicetree";
#endif

// TODO: make the non-fuchsia part of this a general utility that can be shared
// across host-side tests.
void GetTestDataPath(std::string_view filename, std::filesystem::path& path) {
#if defined(__Fuchsia__)
  path.append(kTestDataDir);
#elif defined(__APPLE__)
  uint32_t length = PATH_MAX;
  char self_path[length];
  char self_path_symlink[length];
  _NSGetExecutablePath(self_path_symlink, &length);
  const char* bin_dir = dirname(realpath(self_path_symlink, self_path));
  path.append(bin_dir).append(kTestDataDir);
#elif defined(__linux__)
  char self_path[PATH_MAX];
  const char* bin_dir = dirname(realpath("/proc/self/exe", self_path));
  path.append(bin_dir).append(kTestDataDir);
#else
#error unknown platform.
#endif
  path.append(filename);
}

void ReadTestData(std::string_view filename, cpp20::span<uint8_t> buffer) {
  std::filesystem::path path;
  ASSERT_NO_FATAL_FAILURE(GetTestDataPath(filename, path));

  FILE* file = fopen(path.c_str(), "r");
  ASSERT_NOT_NULL(file, "failed to open %s: %s", path.c_str(), strerror(errno));

  ASSERT_EQ(0, fseek(file, 0, SEEK_END));
  auto size = static_cast<size_t>(ftell(file));
  ASSERT_LE(size, buffer.size(), "file is too large (%zu bytes)", size);
  rewind(file);

  ASSERT_EQ(size, fread(reinterpret_cast<char*>(buffer.data()), 1, size, file));

  ASSERT_EQ(0, fclose(file));
}
