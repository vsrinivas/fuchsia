// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/view.h>

#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "src/lib/files/scoped_temp_dir.h"
#include "tests.h"

namespace {

using JsonBuffer = rapidjson::GenericStringBuffer<rapidjson::UTF8<char>>;

void TestJson(TestDataZbiType type) {
  files::ScopedTempDir dir;
  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  char buff[kMaxZbiSize];
  EXPECT_EQ(static_cast<ssize_t>(size), read(fd.get(), buff, size));

  zbitl::View view(std::string_view{buff, size});

  JsonBuffer buffer;
  rapidjson::PrettyWriter<JsonBuffer> json_writer(buffer);
  json_writer.SetIndent(' ', 2);
  JsonWriteZbi(json_writer, view, 0);
  EXPECT_EQ(GetExpectedJson(type), buffer.GetString());

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error()) << error.error_value().zbi_error << " at offset 0x" << std::hex
                                 << error.error_value().item_offset;
}

TEST(ZbitlJsonTests, EmptyZbi) { ASSERT_NO_FATAL_FAILURE(TestJson(TestDataZbiType::kEmpty)); }

TEST(ZbitlJsonTests, OneItemZbi) { ASSERT_NO_FATAL_FAILURE(TestJson(TestDataZbiType::kOneItem)); }

TEST(ZbitlJsonTests, MultipleSmallItemsZbi) {
  ASSERT_NO_FATAL_FAILURE(TestJson(TestDataZbiType::kMultipleSmallItems));
}

TEST(ZbitlJsonTests, SecondItemOnPageBoundaryZbi) {
  ASSERT_NO_FATAL_FAILURE(TestJson(TestDataZbiType::kSecondItemOnPageBoundary));
}

}  // namespace
