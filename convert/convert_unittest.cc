// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/convert/convert.h"
#include "gtest/gtest.h"

namespace convert {
namespace {

mojo::Array<uint8_t> CreateArray(const char* data, size_t size) {
  mojo::Array<uint8_t> result;
  for (size_t i = 0; i < size; ++i) {
    result.push_back(data[i]);
  }
  return result;
}

TEST(Convert, ToSlice) {
  std::string str = "Hello";
  leveldb::Slice slice = ToSlice(str);
  EXPECT_EQ(str, std::string(slice.data(), slice.size()));

  mojo::Array<uint8_t> array = CreateArray(str.data(), str.size());
  slice = ToSlice(str);
  EXPECT_EQ(str, std::string(slice.data(), slice.size()));
}

TEST(Convert, ToArray) {
  std::string str = "Hello";
  mojo::Array<uint8_t> array = ToArray(str);
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

  mojo::Array<uint8_t> array = ToArray(str);
  result = ToString(slice);
  EXPECT_EQ(str, result);
}

}  // namespace
}  // namespace convert
