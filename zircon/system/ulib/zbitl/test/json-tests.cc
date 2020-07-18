// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/view.h>

#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "corpus.h"
#include "tests.h"

namespace {

using JsonBuffer = rapidjson::GenericStringBuffer<rapidjson::UTF8<char>>;

constexpr char kEmptyZbiJson[] = R"""({
  "offset": 0,
  "type": "CONTAINER",
  "size": 0,
  "items": []
})""";

constexpr char kSimpleZbiJson[] = R"""({
  "offset": 0,
  "type": "CONTAINER",
  "size": 48,
  "items": [
    {
      "offset": 32,
      "type": "CMDLINE",
      "size": 12,
      "crc32": 2172167543
    }
  ]
})""";

TEST(ZbitlViewJsonTests, EmptyZbi) {
  std::string_view zbi{zbitl::test::kEmptyZbi, sizeof(zbitl::test::kEmptyZbi)};
  zbitl::View view(zbi);

  JsonBuffer buffer;
  rapidjson::PrettyWriter<JsonBuffer> json_writer(buffer);
  json_writer.SetIndent(' ', 2);
  JsonWriteZbi(json_writer, view, 0);
  EXPECT_STR_EQ(kEmptyZbiJson, buffer.GetString());

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

TEST(ZbitlViewJsonTests, SimpleZbi) {
  std::string_view zbi{zbitl::test::kSimpleZbi, sizeof(zbitl::test::kSimpleZbi)};
  zbitl::View view(zbi);

  JsonBuffer buffer;
  rapidjson::PrettyWriter<JsonBuffer> json_writer(buffer);
  json_writer.SetIndent(' ', 2);
  JsonWriteZbi(json_writer, view, 0);
  EXPECT_STR_EQ(kSimpleZbiJson, buffer.GetString());

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

}  // namespace
