// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/convert/convert.h"

#include "gtest/gtest.h"
#include "peridot/lib/convert/bytes_test_generated.h"

namespace convert {
namespace {

fidl::Array<uint8_t> CreateArray(const char* data, size_t size) {
  fidl::Array<uint8_t> result;
  for (size_t i = 0; i < size; ++i) {
    result.push_back(data[i]);
  }
  return result;
}

TEST(Convert, ToSlice) {
  std::string str = "Hello";
  leveldb::Slice slice = ToSlice(str);
  EXPECT_EQ(str, std::string(slice.data(), slice.size()));

  fidl::Array<uint8_t> array = CreateArray(str.data(), str.size());
  slice = ToSlice(str);
  EXPECT_EQ(str, std::string(slice.data(), slice.size()));
}

TEST(Convert, ToArray) {
  std::string str = "Hello";
  fidl::Array<uint8_t> array = ToArray(str);
  EXPECT_EQ(str,
            std::string(reinterpret_cast<char*>(array.data()), array.size()));

  leveldb::Slice slice(str.data(), str.size());
  array = ToArray(slice);
  EXPECT_EQ(str,
            std::string(reinterpret_cast<char*>(array.data()), array.size()));
}

TEST(Convert, ToString) {
  std::string str = "Hello";
  leveldb::Slice slice(str.data(), str.size());
  std::string result = ToString(slice);
  EXPECT_EQ(str, result);

  fidl::Array<uint8_t> array = ToArray(str);
  result = ToString(array);
  EXPECT_EQ(str, result);
}

TEST(Convert, ToStringView) {
  std::string str = "Hello";
  leveldb::Slice slice(str.data(), str.size());
  ExtendedStringView result = slice;
  EXPECT_EQ(str, result.ToString());

  fidl::Array<uint8_t> array = ToArray(str);
  result = array;
  EXPECT_EQ(str, result.ToString());
}

TEST(Convert, ToFlatBufferVector) {
  flatbuffers::FlatBufferBuilder builder;

  std::string str = "Hello";
  ExtendedStringView str_view = str;

  auto bytes = str_view.ToFlatBufferVector(&builder);
  builder.Finish(CreateBytesTest(builder, bytes));

  ExtendedStringView result =
      GetBytesTest(builder.GetCurrentBufferPointer())->bytes();
  EXPECT_EQ(str, result);
}

TEST(Convert, ImplicitConversion) {
  std::string str = "Hello";
  ExtendedStringView esv(str);

  leveldb::Slice slice = esv;
  EXPECT_EQ(str, ToString(slice));

  // Suppress check warning that |string_view| is never modified so we could use
  // |esv| instead.
  fxl::StringView string_view = esv;  // NOLINT
  EXPECT_EQ(str, ToString(string_view));
}

}  // namespace
}  // namespace convert
