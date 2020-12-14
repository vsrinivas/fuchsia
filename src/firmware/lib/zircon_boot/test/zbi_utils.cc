// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon_boot/zbi_utils.h>
#include <zircon/boot/image.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace {

TEST(ZbiTests, ZbiFileItemAppend) {
  constexpr char kFileName[] = "file name";
  constexpr size_t kFileNameLen = sizeof(kFileName) - 1;
  constexpr char kFileContent[] = "file content";
  struct {
    zbi_header_t header;
    zbi_header_t file_hdr;
    uint8_t file_payload[ZBI_ALIGN(1 + kFileNameLen + sizeof(kFileContent))];
  } test_zbi;
  ASSERT_EQ(zbi_init(&test_zbi, sizeof(test_zbi)), ZBI_RESULT_OK);
  ASSERT_EQ(AppendZbiFile(&test_zbi.header, sizeof(test_zbi), kFileName, kFileContent,
                          sizeof(kFileContent)),
            ZBI_RESULT_OK);

  ASSERT_EQ(test_zbi.file_hdr.type, ZBI_TYPE_BOOTLOADER_FILE);
  ASSERT_EQ(test_zbi.file_hdr.extra, 0);
  ASSERT_EQ(test_zbi.file_hdr.length, 1 + kFileNameLen + sizeof(kFileContent));
  const uint8_t* payload = test_zbi.file_payload;
  ASSERT_EQ(payload[0], kFileNameLen);
  ASSERT_BYTES_EQ(payload + 1, kFileName, kFileNameLen);
  ASSERT_BYTES_EQ(payload + 1 + kFileNameLen, kFileContent, sizeof(kFileContent));
}

TEST(ZbiTests, NameTooLong) {
  std::string name(257, 'a');
  ASSERT_EQ(AppendZbiFile(nullptr, 0, name.data(), nullptr, 0), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, AppendZbiFilePayloadLengthOverflow) {
  std::string name(10, 'a');
  size_t overflow_size = std::numeric_limits<size_t>::max() - name.size();
  ASSERT_EQ(AppendZbiFile(nullptr, 0, name.data(), nullptr, overflow_size), ZBI_RESULT_TOO_BIG);
}

}  // namespace
