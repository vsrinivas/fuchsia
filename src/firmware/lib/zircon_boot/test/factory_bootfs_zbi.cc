// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon_boot/zbi_utils.h>
#include <lib/zx/result.h>
#include <limits.h>
#include <zircon/boot/bootfs.h>
#include <zircon/boot/image.h>

#include <map>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "rust/factory_bootfs_util/src/factory_bootfs_util.h"

namespace {

struct FactoryFileContext {
  const std::map<std::string, std::string> files;
};

bool ReadFactoryFile(void* context, const char* name, size_t capacity, void* output,
                     size_t* out_len) {
  FactoryFileContext& files_context = *static_cast<FactoryFileContext*>(context);
  auto iter_find = files_context.files.find(name);
  if (iter_find == files_context.files.end() || iter_find->second.size() > capacity) {
    return false;
  }

  memcpy(output, iter_find->second.data(), iter_find->second.size());
  *out_len = iter_find->second.size();
  return true;
}

std::unique_ptr<std::string> ReadBootfsFile(const std::vector<uint8_t>& zbi,
                                            const std::string& name) {
  char buffer[64];
  size_t read_len = sizeof(buffer);
  if (get_bootfs_file_payload(zbi.data(), zbi.size(), name.data(), buffer, &read_len)) {
    return nullptr;
  }

  return std::make_unique<std::string>(std::string(buffer, read_len));
}

constexpr char kTestFile1Name[] = "file1";
constexpr char kTestFile1Content[] = "file1 content";
constexpr char kTestFile2Name[] = "file2";
constexpr char kTestFile2Content[] = "file2 content";

TEST(FactoryBootfsZbiTest, FactoryBootfs) {
  std::vector<const char*> test_files = {
      kTestFile1Name,
      kTestFile2Name,
  };

  struct FactoryFileContext context = {{
      {
          kTestFile1Name,
          kTestFile1Content,
      },
      {
          kTestFile2Name,
          kTestFile2Content,
      },
  }};

  std::vector<uint8_t> buffer(ZBI_BOOTFS_PAGE_SIZE * 10);
  ASSERT_EQ(zbi_init(buffer.data(), buffer.size()), ZBI_RESULT_OK);
  ASSERT_EQ(
      AppendBootfsFactoryFiles(reinterpret_cast<zbi_header_t*>(buffer.data()), buffer.size(),
                               test_files.data(), test_files.size(), ReadFactoryFile, &context),
      ZBI_RESULT_OK);

  // Read and verify kTestFile1Name
  {
    std::unique_ptr<std::string> payload = ReadBootfsFile(buffer, kTestFile1Name);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(kTestFile1Content, *payload);
  }

  // Read and verify kTestFile2Name
  {
    std::unique_ptr<std::string> payload = ReadBootfsFile(buffer, kTestFile2Name);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(kTestFile2Content, *payload);
  }
}

bool FactoryFilePayloadInvalidSize(void* context, const char* name, size_t capacity, void* output,
                                   size_t* out_len) {
  if (std::string(name) == kTestFile1Name) {
    *out_len = (size_t)UINT_MAX + 1;
    return true;
  }

  return ReadFactoryFile(context, name, capacity, output, out_len);
}

TEST(FactoryBootfsZbiTest, FactoryBootfsFileTooLarge) {
  std::vector<const char*> test_files = {
      kTestFile1Name,
      kTestFile2Name,
  };

  struct FactoryFileContext context = {{
      {
          kTestFile1Name,
          kTestFile1Content,
      },
      {
          kTestFile2Name,
          kTestFile2Content,
      },
  }};

  std::vector<uint8_t> buffer(ZBI_BOOTFS_PAGE_SIZE * 10);
  ASSERT_EQ(zbi_init(buffer.data(), buffer.size()), ZBI_RESULT_OK);
  ASSERT_EQ(AppendBootfsFactoryFiles(reinterpret_cast<zbi_header_t*>(buffer.data()), buffer.size(),
                                     test_files.data(), test_files.size(),
                                     FactoryFilePayloadInvalidSize, &context),
            ZBI_RESULT_OK);

  // kTestFile1Name should not be added due to invalid size.
  ASSERT_EQ(nullptr, ReadBootfsFile(buffer, kTestFile1Name));

  // Read and verify kTestFile2Name which should be added.
  std::unique_ptr<std::string> payload = ReadBootfsFile(buffer, kTestFile2Name);
  ASSERT_NE(payload, nullptr);
  ASSERT_EQ(kTestFile2Content, *payload);
}

TEST(FactoryBootfsZbiTest, FactoryBootfsCapacityTooSmall) {
  std::vector<const char*> test_files = {
      kTestFile1Name,
  };

  struct FactoryFileContext context = {{
      {
          kTestFile1Name,
          kTestFile1Content,
      },
  }};

  std::vector<uint8_t> buffer(2048);
  ASSERT_EQ(zbi_init(buffer.data(), buffer.size()), ZBI_RESULT_OK);
  ASSERT_NE(
      AppendBootfsFactoryFiles(reinterpret_cast<zbi_header_t*>(buffer.data()), buffer.size(),
                               test_files.data(), test_files.size(), ReadFactoryFile, &context),
      ZBI_RESULT_OK);
}

TEST(FactoryBootfsZbiTest, FactoryBootfsFileNameTooLong) {
  std::string long_name(512, 'a');
  std::vector<const char*> test_files = {
      long_name.data(),
  };

  struct FactoryFileContext context = {{
      {
          long_name.data(),
          "content",
      },
  }};

  std::vector<uint8_t> buffer(ZBI_BOOTFS_PAGE_SIZE * 10);
  ASSERT_EQ(zbi_init(buffer.data(), buffer.size()), ZBI_RESULT_OK);
  ASSERT_NE(
      AppendBootfsFactoryFiles(reinterpret_cast<zbi_header_t*>(buffer.data()), buffer.size(),
                               test_files.data(), test_files.size(), ReadFactoryFile, &context),
      ZBI_RESULT_OK);
}

TEST(FactoryBootfsZbiTest, FactoryBootfsNoFileName) {
  std::vector<const char*> test_files = {
      "",
  };

  struct FactoryFileContext context = {{
      {
          "",
          "content",
      },
  }};

  std::vector<uint8_t> buffer(ZBI_BOOTFS_PAGE_SIZE * 10);
  ASSERT_EQ(zbi_init(buffer.data(), buffer.size()), ZBI_RESULT_OK);
  ASSERT_NE(
      AppendBootfsFactoryFiles(reinterpret_cast<zbi_header_t*>(buffer.data()), buffer.size(),
                               test_files.data(), test_files.size(), ReadFactoryFile, &context),
      ZBI_RESULT_OK);
}
}  // namespace
