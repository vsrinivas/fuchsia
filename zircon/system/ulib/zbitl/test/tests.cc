// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>

#ifndef __Fuchsia__
#include <libgen.h>
#include <unistd.h>
#endif  // !__Fuchsia__

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "tests.h"

namespace {

#if defined(__Fuchsia__)
constexpr std::string_view kTestDataDir = "/pkg/data";
#else
constexpr std::string_view kTestDataDir = "test_data/zbitl";
#endif

std::string_view ZbiName(TestDataZbiType type) {
  switch (type) {
    case TestDataZbiType::kEmpty:
      return "empty.zbi";
    case TestDataZbiType::kOneItem:
      return "one-item.zbi";
    case TestDataZbiType::kBadCrcItem:
      return "bad-crc-item.zbi";
  }
}

}  // namespace

size_t GetExpectedNumberOfItems(TestDataZbiType type) {
  switch (type) {
    case TestDataZbiType::kEmpty:
      return 0;
    case TestDataZbiType::kOneItem:
      return 1;
    case TestDataZbiType::kBadCrcItem:
      return 1;
  }
}

void GetExpectedPayload(TestDataZbiType type, size_t idx, std::string* payload) {
  size_t num_items = GetExpectedNumberOfItems(type);
  ASSERT_LT(idx, num_items, "expected only %zu items", num_items);
  switch (type) {
    case TestDataZbiType::kEmpty:
      // Assert would have already fired above.
      __UNREACHABLE;
    case TestDataZbiType::kOneItem: {
      *payload = "hello world";
      break;
    }
    case TestDataZbiType::kBadCrcItem: {
      *payload = "hello w\xaa\xaa\xaa\xaa";
      break;
    }
  }
}

std::string GetExpectedJson(TestDataZbiType type) {
  switch (type) {
    case TestDataZbiType::kEmpty:
      return R"""({
  "offset": 0,
  "type": "CONTAINER",
  "size": 0,
  "items": []
})""";
    case TestDataZbiType::kOneItem:
      return R"""({
  "offset": 0,
  "type": "CONTAINER",
  "size": 48,
  "items": [
    {
      "offset": 32,
      "type": "IMAGE_ARGS",
      "size": 11,
      "crc32": 3608077223
    }
  ]
})""";
    case TestDataZbiType::kBadCrcItem:
      // Since computation of the JSON also computes the CRC32, we do not
      // consider this case.
      return "";
  }
}

// TODO: make the non-fuchsia part of this a general utility that can be shared
// across host-side tests.
std::filesystem::path GetTestDataPath(std::string_view filename) {
  std::filesystem::path path;
#if defined(__Fuchsia__)
  path.append(kTestDataDir);
#elif defined(__APPLE__)
  uint32_t length = PATH_MAX;
  char self_path[PATH_MAX];
  char self_path_symlink[PATH_MAX];
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
  return path;
}

void OpenTestDataZbi(TestDataZbiType type, std::string_view work_dir, fbl::unique_fd* fd,
                     size_t* num_bytes) {
  std::string_view filename = ZbiName(type);
  std::filesystem::path path = GetTestDataPath(filename.data());

  // Open a copy of the file, to prevent side-effects from mutating test cases.
  auto copy = std::filesystem::path(work_dir).append(filename.data());
  std::filesystem::copy_file(path, copy);

  *fd = fbl::unique_fd{open(copy.c_str(), O_RDWR)};
  ASSERT_TRUE(fd, "failed to open %.*s: %s", static_cast<int>(filename.size()), filename.data(),
              strerror(errno));

  struct stat st;
  ASSERT_EQ(0, fstat(fd->get(), &st), "failed to stat %.*s: %s", static_cast<int>(filename.size()),
            filename.data(), strerror(errno));
  *num_bytes = static_cast<size_t>(st.st_size);
  ASSERT_LE(*num_bytes, kMaxZbiSize, "file is too large (%zu bytes)", *num_bytes);
}
