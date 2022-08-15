// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/items/debugdata.h>
#include <zircon/boot/image.h>

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

constexpr uint8_t kGoodPayload[] = {
    1,   2,   3,   4, 5,  // contents
    'a', 'b', 'c',        // sink name
    'd', 'e', 'f',        // VMO name
    'l', 'o', 'g',        // log text
    0,   0,               // alignment padding
    5,   0,   0,   0,     // content_size
    3,   0,   0,   0,     // sink_name_size
    3,   0,   0,   0,     // vmo_name_size
    3,   0,   0,   0,     // log_size
};

constexpr uint8_t kBadTrailer[] = {1, 2, 3, 4, 5, 6, 7, 8};

constexpr uint8_t kBadContents[] = {
    1,   2,   3,   4, 5,  // contents
    'a', 'b', 'c',        // sink name
    'd', 'e', 'f',        // VMO name
    'l', 'o', 'g',        // log text
    0,   0,               // alignment padding
    99,  0,   0,   0,     // content_size too big
    3,   0,   0,   0,     // sink_name_size
    3,   0,   0,   0,     // vmo_name_size
    3,   0,   0,   0,     // log_size
};

constexpr uint8_t kBadSink[] = {
    1,   2,   3,   4, 5,  // contents
    'a', 'b', 'c',        // sink name
    'd', 'e', 'f',        // VMO name
    'l', 'o', 'g',        // log text
    0,   0,               // alignment padding
    3,   0,   0,   0,     // content_size
    99,  0,   0,   0,     // sink_name_size too big
    3,   0,   0,   0,     // vmo_name_size
    3,   0,   0,   0,     // log_size
};

constexpr uint8_t kBadVmo[] = {
    1,   2,   3,   4, 5,  // contents
    'a', 'b', 'c',        // sink name
    'd', 'e', 'f',        // VMO name
    'l', 'o', 'g',        // log text
    0,   0,               // alignment padding
    3,   0,   0,   0,     // content_size
    3,   0,   0,   0,     // sink_name_size
    99,  0,   0,   0,     // vmo_name_size too big
    3,   0,   0,   0,     // log_size
};

constexpr uint8_t kBadLog[] = {
    1,   2,   3,   4, 5,  // contents
    'a', 'b', 'c',        // sink name
    'd', 'e', 'f',        // VMO name
    'l', 'o', 'g',        // log text
    0,   0,               // alignment padding
    3,   0,   0,   0,     // content_size
    3,   0,   0,   0,     // sink_name_size
    3,   0,   0,   0,     // vmo_name_size
    99,  0,   0,   0,     // log_size too big
};

constexpr uint8_t kBadAlign[] = {
    1, 2, 3, 4, 5,  // contents
    'a', 'b', 'c',  // sink name
    'd', 'e', 'f',  // VMO name
    'l', 'o', 'g',  // log text
                    // missing alignment padding
    3, 0, 0, 0,     // content_size
    3, 0, 0, 0,     // sink_name_size
    3, 0, 0, 0,     // vmo_name_size
    3, 0, 0, 0,     // log_size too big
};

constexpr uint8_t kBadSize[] = {
    1,   2,   3,   4, 5,           // contents
    'a', 'b', 'c',                 // sink name
    'd', 'e', 'f',                 // VMO name
    'l', 'o', 'g',                 // log text
    0,   0,                        // alignment padding
    0,   0,   0,   0, 0, 0, 0, 0,  // excess padding
    3,   0,   0,   0,              // content_size
    3,   0,   0,   0,              // sink_name_size
    3,   0,   0,   0,              // vmo_name_size
    3,   0,   0,   0,              // log_size too big
};

TEST(ZbitlDebugdataTests, Good) {
  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(kGoodPayload)));
  EXPECT_TRUE(result.is_ok()) << result.error_value();
  EXPECT_EQ(debugdata.sink_name(), "abc");
  EXPECT_EQ(debugdata.vmo_name(), "def");
  EXPECT_EQ(debugdata.log(), "log");
  ASSERT_EQ(debugdata.contents().size(), 5u);
  EXPECT_EQ(0, memcmp(debugdata.contents().data(), kGoodPayload, 5));
}

TEST(ZbitlDebugdataTests, MutableContents) {
  std::array<uint8_t, sizeof(kGoodPayload)> buffer;
  memcpy(buffer.data(), kGoodPayload, buffer.size());

  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(buffer)));
  EXPECT_TRUE(result.is_ok()) << result.error_value();

  ASSERT_EQ(debugdata.contents().size(), 5u);
  EXPECT_EQ(0, memcmp(debugdata.contents().data(), kGoodPayload, 5));

  EXPECT_EQ(debugdata.mutable_contents().data(), debugdata.contents().data());

  constexpr uint8_t kNewContents[] = {6, 7, 8, 9};
  ASSERT_EQ(debugdata.mutable_contents().size(), 5u);
  memcpy(debugdata.mutable_contents().data(), kNewContents, sizeof(kNewContents));
  EXPECT_EQ(0, memcmp(buffer.data(), kNewContents, sizeof(kNewContents)));
}

TEST(ZbitlDebugdataTests, BadTrailer) {
  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(kBadTrailer)));
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), "ZBI_TYPE_DEBUGDATA item too small for debugdata trailer");
}

TEST(ZbitlDebugdataTests, BadContents) {
  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(kBadContents)));
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), "ZBI_TYPE_DEBUGDATA item too small for content size");
}

TEST(ZbitlDebugdataTests, BadSink) {
  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(kBadSink)));
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), "ZBI_TYPE_DEBUGDATA item too small for data-sink name");
}

TEST(ZbitlDebugdataTests, BadVmo) {
  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(kBadVmo)));
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), "ZBI_TYPE_DEBUGDATA item too small for VMO name");
}

TEST(ZbitlDebugdataTests, BadLog) {
  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(kBadLog)));
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), "ZBI_TYPE_DEBUGDATA item too small for log text");
}

TEST(ZbitlDebugdataTests, BadAlign) {
  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(kBadAlign)));
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), "ZBI_TYPE_DEBUGDATA item size not aligned");
}

TEST(ZbitlDebugdataTests, BadSize) {
  zbitl::Debugdata debugdata;
  auto result = debugdata.Init(cpp20::as_bytes(cpp20::span(kBadSize)));
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), "ZBI_TYPE_DEBUGDATA item too large for encoded sizes");
}

}  // namespace
