// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <zircon/boot/image.h>

#ifndef __Fuchsia__
#include <libgen.h>
#include <unistd.h>
#endif  // !__Fuchsia__

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <filesystem>

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
    case TestDataZbiType::kMultipleSmallItems:
      return "multiple-small-items.zbi";
    case TestDataZbiType::kSecondItemOnPageBoundary:
      return "second-item-on-page-boundary.zbi";
  }
}

void GetExpectedPayloadCrc32(TestDataZbiType type, size_t idx, uint32_t* crc) {
  size_t num_items = GetExpectedNumberOfItems(type);
  ASSERT_LT(idx, num_items, "expected only %zu items", num_items);

  switch (type) {
    case TestDataZbiType::kEmpty:
      // Assert would have already fired above.
      __UNREACHABLE;
    case TestDataZbiType::kOneItem:
      *crc = 3608077223;
      break;
    case TestDataZbiType::kBadCrcItem:
      // This function should not be called for this type.
      __UNREACHABLE;
    case TestDataZbiType::kMultipleSmallItems: {
      static const uint32_t crcs[] = {
          3172087020, 2653628068, 1659816855, 2798301622, 833025785,
          1420658445, 1308637244, 764240975,  2938513956, 3173475760,
      };
      *crc = crcs[idx];
      break;
    }
    case TestDataZbiType::kSecondItemOnPageBoundary: {
      static const uint32_t crcs[] = {2447293089, 3746526874};
      *crc = crcs[idx];
      break;
    }
  }
}

}  // namespace

size_t GetExpectedNumberOfItems(TestDataZbiType type) {
  switch (type) {
    case TestDataZbiType::kEmpty:
      return 0;
    case TestDataZbiType::kOneItem:
    case TestDataZbiType::kBadCrcItem:
      return 1;
    case TestDataZbiType::kMultipleSmallItems:
      return 10;
    case TestDataZbiType::kSecondItemOnPageBoundary:
      return 2;
  }
}

void GetExpectedPayload(TestDataZbiType type, size_t idx, Bytes* contents) {
  size_t num_items = GetExpectedNumberOfItems(type);
  ASSERT_LT(idx, num_items, "expected only %zu items", num_items);
  switch (type) {
    case TestDataZbiType::kEmpty:
      // Assert would have already fired above.
      __UNREACHABLE;
    case TestDataZbiType::kOneItem: {
      *contents = "hello world";
      return;
    }
    case TestDataZbiType::kBadCrcItem: {
      *contents = "hello w\xaa\xaa\xaa\xaa";
      return;
    }
    case TestDataZbiType::kMultipleSmallItems: {
      static const char* const payloads[] = {
          "Four score and seven years ago our fathers brought forth on this continent, a new "
          "nation, conceived in Liberty, and dedicated to the proposition that all men are created "
          "equal.",
          "Now we are engaged in a great civil war, testing whether that nation, or any nation so "
          "conceived and so dedicated, can long endure.",
          "We are met on a great battle-field of that war.",
          "We have come to dedicate a portion of that field, as a final resting place for those "
          "who here gave their lives that that nation might live.",
          "It is altogether fitting and proper that we should do this.",
          "But, in a larger sense, we can not dedicate -- we can not consecrate -- we can not "
          "hallow -- this ground.",
          "The brave men, living and dead, who struggled here, have consecrated it, far above our "
          "poor power to add or detract.",
          "The world will little note, nor long remember what we say here, but it can never forget "
          "what they did here.",
          "It is for us the living, rather, to be dedicated here to the unfinished work which they "
          "who fought here have thus far so nobly advanced.",
          "It is rather for us to be here dedicated to the great task remaining before us -- that "
          "from these honored dead we take increased devotion to that cause for which they gave "
          "the last full measure of devotion -- that we here highly resolve that these dead shall "
          "not have died in vain -- that this nation, under God, shall have a new birth of freedom "
          "-- and that government of the people, by the people, for the people, shall not perish "
          "from the earth.",
      };
      *contents = payloads[idx];
      return;
    }
    case TestDataZbiType::kSecondItemOnPageBoundary: {
      static const char* const payloads[] = {
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
          "YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY",
      };
      *contents = payloads[idx];
      return;
    }
  }
}

void GetExpectedPayloadWithHeader(TestDataZbiType type, size_t idx, Bytes* contents) {
  Bytes payload;
  ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx, &payload));

  uint32_t crc;
  ASSERT_NO_FATAL_FAILURES(GetExpectedPayloadCrc32(type, idx, &crc));

  zbi_header_t header{};
  header.type = ZBI_TYPE_IMAGE_ARGS;
  header.magic = ZBI_ITEM_MAGIC;
  header.flags = ZBI_FLAG_VERSION | ZBI_FLAG_CRC32;
  header.length = static_cast<uint32_t>(payload.size());
  header.crc32 = crc;

  *contents = {reinterpret_cast<char*>(&header), sizeof(zbi_header_t)};
  contents->append(payload);
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
    case TestDataZbiType::kMultipleSmallItems:
      return R"""({
  "offset": 0,
  "type": "CONTAINER",
  "size": 1816,
  "items": [
    {
      "offset": 32,
      "type": "IMAGE_ARGS",
      "size": 176,
      "crc32": 3172087020
    },
    {
      "offset": 240,
      "type": "IMAGE_ARGS",
      "size": 131,
      "crc32": 2653628068
    },
    {
      "offset": 408,
      "type": "IMAGE_ARGS",
      "size": 47,
      "crc32": 1659816855
    },
    {
      "offset": 488,
      "type": "IMAGE_ARGS",
      "size": 139,
      "crc32": 2798301622
    },
    {
      "offset": 664,
      "type": "IMAGE_ARGS",
      "size": 59,
      "crc32": 833025785
    },
    {
      "offset": 760,
      "type": "IMAGE_ARGS",
      "size": 105,
      "crc32": 1420658445
    },
    {
      "offset": 904,
      "type": "IMAGE_ARGS",
      "size": 116,
      "crc32": 1308637244
    },
    {
      "offset": 1056,
      "type": "IMAGE_ARGS",
      "size": 107,
      "crc32": 764240975
    },
    {
      "offset": 1200,
      "type": "IMAGE_ARGS",
      "size": 136,
      "crc32": 2938513956
    },
    {
      "offset": 1368,
      "type": "IMAGE_ARGS",
      "size": 448,
      "crc32": 3173475760
    }
  ]
})""";
    case TestDataZbiType::kSecondItemOnPageBoundary:
      return R"""({
  "offset": 0,
  "type": "CONTAINER",
  "size": 4128,
  "items": [
    {
      "offset": 32,
      "type": "IMAGE_ARGS",
      "size": 4032,
      "crc32": 2447293089
    },
    {
      "offset": 4096,
      "type": "IMAGE_ARGS",
      "size": 32,
      "crc32": 3746526874
    }
  ]
})""";
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
