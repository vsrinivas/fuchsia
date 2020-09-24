// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/clone.h"

#include <gtest/gtest.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/socket.h>
#endif

namespace fidl {
namespace {

TEST(Clone, Control) {
  int8_t a = 32;
  int8_t b = 0;
  EXPECT_EQ(ZX_OK, Clone(a, &b));
  EXPECT_EQ(32, b);
}

#ifdef __Fuchsia__
TEST(Clone, Socket) {
  zx::socket h1, h2;
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &h1, &h2));
  zx::socket h;

  EXPECT_EQ(ZX_OK, Clone(h1, &h));
  h.reset();
  EXPECT_EQ(ZX_OK, Clone(h, &h2));
  EXPECT_FALSE(h2.is_valid());
}

TEST(Clone, Channel) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx::channel h;

  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, Clone(h1, &h));
  EXPECT_EQ(ZX_OK, Clone(h, &h2));
  EXPECT_FALSE(h2.is_valid());
}
#endif

TEST(Clone, VectorPtrPrimitive) {
  VectorPtr<int> intvec1({1, 2, 3, 4});
  VectorPtr<int> intvec2;

  EXPECT_TRUE(intvec1.has_value());
  EXPECT_FALSE(intvec2.has_value());
  EXPECT_EQ(ZX_OK, Clone(intvec1, &intvec2));
  EXPECT_TRUE(intvec1.has_value());
  EXPECT_TRUE(intvec2.has_value());
  EXPECT_EQ(intvec1->size(), 4u);
  EXPECT_EQ(intvec2->size(), 4u);
  EXPECT_EQ(intvec2->at(0), 1);
  EXPECT_EQ(intvec2->at(1), 2);
  EXPECT_EQ(intvec2->at(2), 3);
  EXPECT_EQ(intvec2->at(3), 4);

  intvec1->push_back(5);
  EXPECT_EQ(intvec1->size(), 5u);
  EXPECT_EQ(intvec2->size(), 4u);

  VectorPtr<int> intvec3;
  EXPECT_TRUE(intvec2.has_value());
  EXPECT_FALSE(intvec3.has_value());
  EXPECT_EQ(ZX_OK, Clone(intvec3, &intvec2));
  EXPECT_FALSE(intvec2.has_value());
  EXPECT_FALSE(intvec3.has_value());

  VectorPtr<int> empty;
  empty.emplace();
  EXPECT_TRUE(empty.has_value());
  EXPECT_EQ(empty->size(), 0u);
  auto cloned_empty = fidl::Clone(empty);
  EXPECT_TRUE(cloned_empty.has_value());
  EXPECT_EQ(cloned_empty->size(), 0u);
}

TEST(Clone, VectorPtrString) {
  VectorPtr<std::string> strvec1({"satu", "dua", "tiga", "empat"});
  VectorPtr<std::string> strvec2;
  VectorPtr<std::string> strvec3;

  EXPECT_TRUE(strvec1.has_value());
  EXPECT_FALSE(strvec2.has_value());
  EXPECT_EQ(ZX_OK, Clone(strvec1, &strvec2));
  EXPECT_TRUE(strvec1.has_value());
  EXPECT_TRUE(strvec2.has_value());
  EXPECT_EQ(strvec1->size(), 4u);
  EXPECT_EQ(strvec2->size(), 4u);
  EXPECT_EQ(strvec2->at(0), "satu");
  EXPECT_EQ(strvec2->at(1), "dua");
  EXPECT_EQ(strvec2->at(2), "tiga");
  EXPECT_EQ(strvec2->at(3), "empat");

  strvec1->push_back("lima");
  EXPECT_EQ(strvec1->size(), 5u);
  EXPECT_EQ(strvec2->size(), 4u);

  EXPECT_TRUE(strvec2.has_value());
  EXPECT_FALSE(strvec3.has_value());
  EXPECT_EQ(ZX_OK, Clone(strvec3, &strvec2));
  EXPECT_FALSE(strvec2.has_value());
  EXPECT_FALSE(strvec3.has_value());

  VectorPtr<std::string> empty;
  empty.emplace();
  EXPECT_TRUE(empty.has_value());
  EXPECT_EQ(empty->size(), 0u);
  auto cloned_empty = fidl::Clone(empty);
  EXPECT_TRUE(cloned_empty.has_value());
  EXPECT_EQ(cloned_empty->size(), 0u);
}

// There's a different implementation for Clone<VectorPtr<T>> for strings and
// primitives vs all other types.
TEST(Clone, VectorPtrVector) {
  VectorPtr<std::vector<int>> vecvec1({std::vector<int>(1, 1), std::vector<int>(2, 2),
                                       std::vector<int>(3, 3), std::vector<int>(4, 4)});
  VectorPtr<std::vector<int>> vecvec2;
  VectorPtr<std::vector<int>> vecvec3;

  EXPECT_TRUE(vecvec1.has_value());
  EXPECT_FALSE(vecvec2.has_value());
  EXPECT_EQ(ZX_OK, Clone(vecvec1, &vecvec2));
  EXPECT_TRUE(vecvec1.has_value());
  EXPECT_TRUE(vecvec2.has_value());
  EXPECT_EQ(vecvec1->size(), 4u);
  EXPECT_EQ(vecvec2->size(), 4u);
  EXPECT_EQ(vecvec2->at(0).size(), 1u);
  EXPECT_EQ(vecvec2->at(1).size(), 2u);
  EXPECT_EQ(vecvec2->at(2).size(), 3u);
  EXPECT_EQ(vecvec2->at(3).size(), 4u);

  vecvec1->push_back(std::vector<int>(5, 5));
  EXPECT_EQ(vecvec1->size(), 5u);
  EXPECT_EQ(vecvec2->size(), 4u);

  EXPECT_TRUE(vecvec2.has_value());
  EXPECT_FALSE(vecvec3.has_value());
  EXPECT_EQ(ZX_OK, Clone(vecvec3, &vecvec2));
  EXPECT_FALSE(vecvec2.has_value());
  EXPECT_FALSE(vecvec3.has_value());

  VectorPtr<std::vector<int>> empty;
  empty.emplace();
  EXPECT_TRUE(empty.has_value());
  EXPECT_EQ(empty->size(), 0u);
  auto cloned_empty = fidl::Clone(empty);
  EXPECT_TRUE(cloned_empty.has_value());
  EXPECT_EQ(cloned_empty->size(), 0u);
}

TEST(Clone, UnknownBytes) {
  UnknownBytes bytes = {
      .bytes = {0xde, 0xad, 0xbe, 0xef},
  };

  UnknownBytes copy;
  ASSERT_EQ(ZX_OK, Clone(bytes, &copy));
  ASSERT_EQ(copy.bytes.size(), 4u);
  EXPECT_EQ(copy.bytes[0], 0xde);
  EXPECT_EQ(copy.bytes[1], 0xad);
  EXPECT_EQ(copy.bytes[2], 0xbe);
  EXPECT_EQ(copy.bytes[3], 0xef);
}

#ifdef __Fuchsia__
TEST(Clone, UnknownHandles) {
  zx_handle_t h;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h));
  UnknownData data = {
      .bytes = {0xde, 0xad},
      .handles = {},
  };
  data.handles.push_back(zx::handle(h));

  UnknownData copy;
  ASSERT_EQ(ZX_OK, Clone(data, &copy));

  ASSERT_EQ(copy.bytes.size(), 2u);
  EXPECT_EQ(copy.bytes[0], 0xde);
  EXPECT_EQ(copy.bytes[1], 0xad);

  ASSERT_EQ(copy.handles.size(), 1u);
  zx_info_handle_basic_t info_orig = {};
  ASSERT_EQ(ZX_OK, data.handles[0].get_info(ZX_INFO_HANDLE_BASIC, &info_orig, sizeof(info_orig),
                                            nullptr, nullptr));
  zx_info_handle_basic_t info_copy = {};
  ASSERT_EQ(ZX_OK, data.handles[0].get_info(ZX_INFO_HANDLE_BASIC, &info_copy, sizeof(info_copy),
                                            nullptr, nullptr));
  EXPECT_EQ(info_orig.koid, info_copy.koid);
}
#endif

}  // namespace
}  // namespace fidl
