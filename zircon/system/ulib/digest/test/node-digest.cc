// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <digest/node-digest.h>
#include <fbl/span.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

namespace digest {
namespace {

using ::testing::ElementsAreArray;

void TestGeometry(size_t node_size) {
  NodeDigest node_digest;
  size_t data_off;

  node_digest.SetNodeSize(node_size);
  EXPECT_EQ(node_digest.node_size(), node_size);
  EXPECT_TRUE(node_digest.IsAligned(0));

  data_off = node_size;
  EXPECT_TRUE(node_digest.IsAligned(data_off));
  EXPECT_EQ(node_digest.ToNode(data_off), 1ul);
  EXPECT_EQ(node_digest.PrevAligned(data_off), data_off);
  EXPECT_EQ(node_digest.NextAligned(data_off), data_off);

  data_off = node_size - 1;
  EXPECT_FALSE(node_digest.IsAligned(data_off));
  EXPECT_EQ(node_digest.ToNode(data_off), 0ul);
  EXPECT_EQ(node_digest.PrevAligned(data_off), 0ul);
  EXPECT_EQ(node_digest.NextAligned(data_off), node_size);

  data_off = node_size + 1;
  EXPECT_FALSE(node_digest.IsAligned(data_off));
  EXPECT_EQ(node_digest.ToNode(data_off), 1ul);
  EXPECT_EQ(node_digest.PrevAligned(data_off), node_size);
  EXPECT_EQ(node_digest.NextAligned(data_off), node_size * 2);

  data_off = node_size * 37;
  EXPECT_TRUE(node_digest.IsAligned(data_off));
  EXPECT_EQ(node_digest.ToNode(data_off), 37ul);
  EXPECT_EQ(node_digest.PrevAligned(data_off), data_off);
  EXPECT_EQ(node_digest.NextAligned(data_off), data_off);

  EXPECT_LT(SIZE_MAX - node_digest.MaxAligned(), node_size);
}
TEST(NodeDigest, Geometry) {
  NodeDigest node_digest;
  EXPECT_STATUS(node_digest.SetNodeSize(0), ZX_ERR_INVALID_ARGS);
  for (size_t node_size = 1; node_size != 0; node_size *= 2) {
    EXPECT_STATUS(node_digest.SetNodeSize(node_size - 1), ZX_ERR_INVALID_ARGS);
    if (node_size < kMinNodeSize || kMaxNodeSize < node_size) {
      EXPECT_STATUS(node_digest.SetNodeSize(node_size), ZX_ERR_INVALID_ARGS);
    } else {
      TestGeometry(node_size);
    }
    EXPECT_STATUS(node_digest.SetNodeSize(node_size + 1), ZX_ERR_INVALID_ARGS);
  }
}

TEST(NodeDigest, ResetAndAppend) {
  NodeDigest node_digest;
  size_t node_size = node_digest.node_size();
  EXPECT_STATUS(node_digest.Reset(node_size, 0), ZX_ERR_INVALID_ARGS);              // Out of bounds
  EXPECT_STATUS(node_digest.Reset(node_size - 1, node_size), ZX_ERR_INVALID_ARGS);  // Unaligned

  uint8_t data[kDefaultNodeSize];
  ASSERT_EQ(node_size, sizeof(data));
  memset(data, 0xff, sizeof(data));
  struct {
    uint64_t id;
    size_t off;
    size_t len;
    const char *hex;
  } tc[] = {
      {0, 0, 0, "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"},
      {0, 0, 1, "0967e0f62a104d1595610d272dfab3d2fa2fe07be0eebce13ef5d79db142610e"},
      {0, 0, node_size / 2, "0a90612c255555469dead72c8fdc41eec06dfe04a30a1f2b7c480ff95d20c5ec"},
      {0, 0, node_size - 1, "f2abd690381bab3ce485c814d05c310b22c34a7441418b5c1a002c344a80e730"},
      {0, 0, node_size, "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737"},
      {0, node_size, node_size, "3464d7bd8ff9d47bfd613997f8ba15dac713a40cf3767fbb0a9d318079e6f070"},
      {1, node_size, node_size, "3759236f044880c85a4c9fb16866585f34fdc6b604435a968581a0e8c4176125"},
  };
  const Digest &actual = node_digest.get();
  Digest expected;
  for (size_t i = 0; i < sizeof(tc) / sizeof(tc[0]); ++i) {
    SCOPED_TRACE(std::string("Test case with digest: ") + tc[i].hex);
    node_digest.set_id(tc[i].id);
    size_t data_off = tc[i].off;
    size_t data_len = tc[i].len;
    ASSERT_OK(expected.Parse(tc[i].hex));
    // All at once
    EXPECT_OK(node_digest.Reset(data_off, data_off + data_len));
    EXPECT_EQ(node_digest.Append(data, SIZE_MAX), data_len);
    EXPECT_THAT(fbl::Span(actual.get(), kSha256Length),
                ElementsAreArray(expected.get(), kSha256Length));
    // Byte by byte
    EXPECT_OK(node_digest.Reset(data_off, data_off + data_len));
    for (size_t i = data_off; i < data_len; ++i) {
      EXPECT_EQ(node_digest.Append(data, 1), 1ul);
    }
    EXPECT_THAT(fbl::Span(actual.get(), kSha256Length),
                ElementsAreArray(expected.get(), kSha256Length));
  }
}

TEST(NodeDigest, MinNodeSizeIsValid) { EXPECT_TRUE(NodeDigest::IsValidNodeSize(kMinNodeSize)); }

TEST(NodeDigest, MaxNodeSizeIsValid) { EXPECT_TRUE(NodeDigest::IsValidNodeSize(kMaxNodeSize)); }

TEST(NodeDigest, DefaultNodeSizeIsValid) {
  EXPECT_TRUE(NodeDigest::IsValidNodeSize(kDefaultNodeSize));
}

TEST(NodeDigest, NodeSizeLessThanMinIsInvalid) {
  EXPECT_FALSE(NodeDigest::IsValidNodeSize(kMinNodeSize >> 1));
}

TEST(NodeDigest, NodeSizeGreaterThanMaxIsInvalid) {
  EXPECT_FALSE(NodeDigest::IsValidNodeSize(kMaxNodeSize << 1));
}

TEST(NodeDigest, NodeSizeNotPowerOf2IsInvalid) {
  EXPECT_FALSE(NodeDigest::IsValidNodeSize(kMaxNodeSize - 1));
}

}  // namespace
}  // namespace digest
