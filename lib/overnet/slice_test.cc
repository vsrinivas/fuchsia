// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "slice.h"
#include "gtest/gtest.h"

namespace overnet {

TEST(Slice, Empty) {
  Slice slice;
  EXPECT_EQ(slice.begin(), slice.end());
  EXPECT_EQ(0u, slice.length());
}

TEST(Slice, Static) {
  auto slice = Slice::FromStaticString("Hello World!");
  EXPECT_EQ("Hello World!", slice.AsStdString());
  auto slice2 = slice;
  EXPECT_EQ("Hello World!", slice.AsStdString());
  EXPECT_EQ("Hello World!", slice2.AsStdString());
  slice2 = Slice::FromStaticString("Goodbye");
  EXPECT_EQ("Goodbye", slice2.AsStdString());
  slice = slice2;
  EXPECT_EQ("Goodbye", slice2.AsStdString());
  EXPECT_EQ("Goodbye", slice.AsStdString());
  slice.TrimBegin(4);
  EXPECT_EQ("bye", slice.AsStdString());
  slice2.TrimEnd(3);
  EXPECT_EQ("Good", slice2.AsStdString());
}

TEST(Slice, ShortCopied) {
  uint8_t data[] = {1, 2, 3};
  auto slice = Slice::FromCopiedBuffer(data, sizeof(data));
  EXPECT_EQ(slice.length(), 3u);
  EXPECT_EQ(slice.begin()[0], 1);
  EXPECT_EQ(slice.begin()[1], 2);
  EXPECT_EQ(slice.begin()[2], 3);
  auto slice2 = slice;
  EXPECT_EQ(slice2.length(), 3u);
}

TEST(Slice, BigCopied) {
  static const size_t kLength = 1024 * 1024;
  auto data = std::unique_ptr<uint8_t[]>(new uint8_t[kLength]);
  for (size_t i = 0; i < kLength; i++) {
    data[i] = i & 0xff;
  }
  auto slice = Slice::FromCopiedBuffer(data.get(), kLength);
  EXPECT_EQ(kLength, slice.length());
  for (size_t i = 0; i < kLength; i++) {
    EXPECT_EQ(i & 0xff, slice.begin()[i]);
  }
  auto slice2 = slice;
  EXPECT_EQ(kLength, slice2.length());
  EXPECT_EQ(kLength, slice.length());
  auto slice3 = std::move(slice);
  EXPECT_NE(kLength, slice.length());
  EXPECT_EQ(kLength, slice2.length());
  EXPECT_EQ(kLength, slice3.length());
}

TEST(Slice, Join) {
  auto a = Slice::FromStaticString("ABC");
  uint8_t data[] = {1, 2, 3};
  auto b = Slice::FromCopiedBuffer(data, sizeof(data));
  static const size_t kLength = 1024 * 1024;
  auto data2 = std::unique_ptr<uint8_t[]>(new uint8_t[kLength]);
  for (size_t i = 0; i < kLength; i++) {
    data2[i] = i & 0xff;
  }
  auto c = Slice::FromCopiedBuffer(data2.get(), kLength);

  auto joined = Slice::Join({a, b, c});
  EXPECT_EQ(joined.length(), kLength + 6);
  EXPECT_EQ(joined.begin()[0], 'A');
  EXPECT_EQ(joined.begin()[1], 'B');
  EXPECT_EQ(joined.begin()[2], 'C');
  EXPECT_EQ(joined.begin()[3], 1);
  EXPECT_EQ(joined.begin()[4], 2);
  EXPECT_EQ(joined.begin()[5], 3);
  EXPECT_EQ(joined.begin()[6], 0);
}

TEST(Slice, Static_WithPrefix) {
  auto slice = Slice::FromStaticString("ABC");
  auto slice2 =
      slice.WithPrefix(3, [](uint8_t* bytes) { memcpy(bytes, "123", 3); });
  EXPECT_EQ(slice.AsStdString(), "ABC");
  EXPECT_EQ(slice2.AsStdString(), "123ABC");
}

TEST(Slice, Short_WithPrefix) {
  uint8_t data[] = {1, 2, 3};
  auto slice = Slice::FromCopiedBuffer(data, sizeof(data));
  auto slice2 = slice.WithPrefix(3, [](uint8_t* bytes) {
    bytes[0] = 7;
    bytes[1] = 8;
    bytes[2] = 9;
  });
  EXPECT_EQ(slice.length(), 3u);
  EXPECT_EQ(slice.begin()[0], 1);
  EXPECT_EQ(slice.begin()[1], 2);
  EXPECT_EQ(slice.begin()[2], 3);
  EXPECT_EQ(slice2.length(), 6u);
  EXPECT_EQ(slice2.begin()[0], 7);
  EXPECT_EQ(slice2.begin()[1], 8);
  EXPECT_EQ(slice2.begin()[2], 9);
  EXPECT_EQ(slice2.begin()[3], 1);
  EXPECT_EQ(slice2.begin()[4], 2);
  EXPECT_EQ(slice2.begin()[5], 3);
}

TEST(Slice, Big_WithPrefix) {
  static const size_t kLength = 1024 * 1024;
  auto data = std::unique_ptr<uint8_t[]>(new uint8_t[kLength]);
  for (size_t i = 0; i < kLength; i++) {
    data[i] = i & 0xff;
  }
  auto slice = Slice::FromCopiedBuffer(data.get(), kLength);
  auto slice2 = slice.WithPrefix(3, [](uint8_t* bytes) {
    bytes[0] = 7;
    bytes[1] = 8;
    bytes[2] = 9;
  });
  EXPECT_EQ(kLength, slice.length());
  EXPECT_EQ(kLength + 3, slice2.length());
  EXPECT_EQ(7, slice2.begin()[0]);
  EXPECT_EQ(8, slice2.begin()[1]);
  EXPECT_EQ(9, slice2.begin()[2]);
  for (size_t i = 0; i < kLength; i++) {
    EXPECT_EQ(i & 0xff, slice.begin()[i]);
    EXPECT_EQ(i & 0xff, slice2.begin()[i + 3]);
  }
}

TEST(Slice, Ostream) {
  std::ostringstream out;
  out << Slice::FromStaticString("ABC") << 100;
  EXPECT_EQ(out.str(), "[41 42 43]100");
}

}  // namespace overnet
