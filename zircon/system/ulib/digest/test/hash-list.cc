// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>

#include <digest/digest.h>
#include <digest/hash-list.h>
#include <digest/node-digest.h>
#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

namespace digest {
namespace testing {

// Sizes for testing
static const size_t kNodeSize = kMinNodeSize;
static const size_t kNumNodes = 4;
static const size_t kDataLen = kNodeSize * kNumNodes - 1;
static const size_t kListLen = kSha256Length * kNumNodes;

TEST(HashListBase, Align) {
  internal::HashListBase base;
  ASSERT_OK(base.SetNodeSize(kNodeSize));
  base.SetDataLength(kDataLen);
  size_t off, len, end;
  zx_status_t rc;
  // Sample a combination of aligned and unaligned offsets and lengths
  for (size_t i = 0; i < kDataLen; i += 16) {
    for (size_t j = 0; j < kDataLen; j += 32) {
      off = i;
      len = j;
      end = i + j;
      rc = base.Align(&off, &len);
      // Check if i+j in/out of range is detected correctly.
      if (end > kDataLen) {
        EXPECT_STATUS(rc, ZX_ERR_OUT_OF_RANGE);
        continue;
      }
      EXPECT_OK(rc);
      // Check that off is a tight, node aligned bound.
      EXPECT_LE(off, i);
      EXPECT_GT(off + kNodeSize, i);
      EXPECT_EQ(off % kNodeSize, 0);
      // Check that len is node aligned, or runs to end of data.
      EXPECT_GE(off + len, end);
      EXPECT_LT(off + len, end + kNodeSize);
      if (end > fbl::round_down(kDataLen, kNodeSize)) {
        EXPECT_EQ(len, kDataLen - off);
      } else {
        EXPECT_EQ(len % kNodeSize, 0);
      }
    }
  }
}

TEST(HashListBase, GetListLength) {
  internal::HashListBase base;
  ASSERT_OK(base.SetNodeSize(kNodeSize));
  // Special case: zero length
  EXPECT_OK(base.SetDataLength(0));
  EXPECT_EQ(base.GetListLength(), kSha256Length);
  for (size_t i = 1; i < kDataLen; ++i) {
    EXPECT_OK(base.SetDataLength(i));
    EXPECT_EQ(base.GetListLength(), ((i + kNodeSize - 1) / kNodeSize) * kSha256Length);
  }
}

template <typename T>
void TestSetList(size_t data_len, size_t list_len) {
  internal::HashList<T> base;
  ASSERT_OK(base.SetNodeSize(kNodeSize));
  uint8_t list[kListLen];

  ASSERT_LE(list_len, sizeof(list));
  ASSERT_OK(base.SetDataLength(data_len));
  EXPECT_EQ(base.data_off(), 0);
  EXPECT_EQ(base.data_len(), data_len);
  ASSERT_LE(base.GetListLength(), sizeof(list));
  EXPECT_EQ(base.GetListLength(), list_len);
  EXPECT_STATUS(base.SetList(nullptr, list_len), ZX_ERR_INVALID_ARGS);
  EXPECT_STATUS(base.SetList(list, list_len - 1), ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_OK(base.SetList(list, list_len));
  EXPECT_EQ(base.list(), list);
  EXPECT_EQ(base.list_len(), list_len);
}
TEST(HashList, SetList) {
  ASSERT_NO_FATAL_FAILURES(TestSetList<uint8_t>(0, kSha256Length));
  ASSERT_NO_FATAL_FAILURES(TestSetList<uint8_t>(kDataLen, kListLen));
  ASSERT_NO_FATAL_FAILURES(TestSetList<const uint8_t>(0, kSha256Length));
  ASSERT_NO_FATAL_FAILURES(TestSetList<const uint8_t>(kDataLen, kListLen));
}

TEST(HashListCreator, Append) {
  HashListCreator creator;
  uint8_t buf[kDataLen];
  memset(buf, 0, sizeof(buf));
  uint8_t list[kListLen];

  // Empty list
  EXPECT_OK(creator.SetDataLength(0));

  // No SetList
  EXPECT_OK(creator.SetDataLength(kDataLen));
  EXPECT_STATUS(creator.Append(buf, sizeof(buf)), ZX_ERR_BAD_STATE);

  // Works for aligned sizes
  EXPECT_OK(creator.SetList(list, sizeof(list)));
  EXPECT_OK(creator.Append(buf, kNodeSize));
  EXPECT_OK(creator.Append(buf, kNodeSize));

  // Works for unaligned sizes
  for (size_t i = 0; i < 16; ++i) {
    EXPECT_OK(creator.Append(buf, i));
  }

  // Fails with too much data
  EXPECT_STATUS(creator.Append(buf, kDataLen), ZX_ERR_INVALID_ARGS);

  // Can restart, and submit all data at once
  EXPECT_OK(creator.SetList(list, sizeof(list)));
  EXPECT_OK(creator.Append(buf, sizeof(buf)));
}

TEST(HashListVerifier, Verify) {
  uint8_t buf[kDataLen];
  srand(0);
  for (size_t i = 0; i < kDataLen; ++i) {
    buf[i] = static_cast<uint8_t>(rand());
  }
  uint8_t list[kListLen];

  HashListCreator creator;
  creator.SetNodeId(1);
  ASSERT_OK(creator.SetNodeSize(kNodeSize));

  HashListVerifier verifier;
  verifier.SetNodeId(1);
  ASSERT_OK(verifier.SetNodeSize(kNodeSize));

  // Empty list
  EXPECT_OK(creator.SetDataLength(0));
  EXPECT_OK(creator.SetList(list, sizeof(list)));
  EXPECT_OK(verifier.SetDataLength(0));
  EXPECT_OK(verifier.SetList(list, sizeof(list)));
  EXPECT_OK(verifier.Verify(nullptr, 0, 0));

  // No SetList
  EXPECT_OK(creator.SetDataLength(kDataLen));
  EXPECT_OK(creator.SetList(list, sizeof(list)));
  EXPECT_OK(creator.Append(buf, sizeof(buf)));
  EXPECT_OK(verifier.SetDataLength(kDataLen));
  EXPECT_STATUS(verifier.Verify(buf, sizeof(buf), 0), ZX_ERR_BAD_STATE);

  // Empty range is trivial true
  EXPECT_OK(verifier.SetList(list, sizeof(list)));
  for (size_t i = 0; i < kDataLen; i += kNodeSize) {
    EXPECT_OK(verifier.Verify(buf, 0, i));
  }

  // Can verify all at once
  EXPECT_OK(verifier.Verify(buf, sizeof(buf), 0));

  // Wrong ID
  HashListVerifier wrong_id;
  wrong_id.SetNodeId(2);
  ASSERT_OK(wrong_id.SetNodeSize(kNodeSize));
  EXPECT_OK(wrong_id.SetDataLength(kDataLen));
  EXPECT_OK(wrong_id.SetList(list, sizeof(list)));
  EXPECT_STATUS(wrong_id.Verify(buf, sizeof(buf), 0), ZX_ERR_IO_DATA_INTEGRITY);

  // Can verify a subset
  for (size_t i = 0; i < kDataLen; i += kNodeSize) {
    for (size_t j = 0; j < kDataLen; j += kNodeSize) {
      if (i + j < kDataLen) {
        EXPECT_OK(verifier.Verify(&buf[i], j, i));
      } else {
        EXPECT_OK(verifier.Verify(&buf[i], kDataLen - i, i));
      }
    }

    // Flipped byte causes failure (but only in affected nodes)
    size_t before = fbl::round_down(i, kNodeSize);
    size_t after = std::min(fbl::round_up(i + 1, kNodeSize), sizeof(buf));
    size_t affected = std::min(kNodeSize, sizeof(buf) - before);
    size_t trailing = sizeof(buf) - after;
    size_t k = i + (rand() % affected);
    buf[k] ^= 0xFF;
    EXPECT_STATUS(verifier.Verify(buf, sizeof(buf), 0), ZX_ERR_IO_DATA_INTEGRITY);
    EXPECT_OK(verifier.Verify(buf, before, 0));
    EXPECT_STATUS(verifier.Verify(&buf[before], affected, before), ZX_ERR_IO_DATA_INTEGRITY);
    EXPECT_OK(verifier.Verify(&buf[after], trailing, after));
    buf[k] ^= 0xFF;
  }
}

TEST(HashList, CalculateHashListSize) {
  EXPECT_EQ(kSha256Length, CalculateHashListSize(0, kDefaultNodeSize));
  EXPECT_EQ(kSha256Length, CalculateHashListSize(1, kDefaultNodeSize));
  EXPECT_EQ(kSha256Length, CalculateHashListSize(10, kDefaultNodeSize));
  EXPECT_EQ(kSha256Length, CalculateHashListSize(kDefaultNodeSize - 1, kDefaultNodeSize));
  EXPECT_EQ(kSha256Length, CalculateHashListSize(kDefaultNodeSize, kDefaultNodeSize));
  EXPECT_EQ(kSha256Length * 2, CalculateHashListSize(kDefaultNodeSize + 1, kDefaultNodeSize));
  EXPECT_EQ(kSha256Length * 40, CalculateHashListSize(kDefaultNodeSize * 40, kDefaultNodeSize));
  EXPECT_EQ(kSha256Length * 41, CalculateHashListSize(kDefaultNodeSize * 40 + 1, kDefaultNodeSize));
}

}  // namespace testing
}  // namespace digest
