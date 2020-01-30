// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"

#include <zircon/errors.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan {
namespace brcmfmac {
namespace {

TEST(Netbuf, EthernetNetbuf) {
  // Create an EthernetNetbuf, then allow it to automatically Return().
  bool err_internal_returned = false;
  {
    auto eth_netbuf = std::make_unique<ethernet_netbuf_t>();
    auto err_internal_callback = [&](zx_status_t status, ethernet_netbuf_t* netbuf) {
      EXPECT_EQ(ZX_ERR_INTERNAL, status);
      EXPECT_EQ(eth_netbuf.get(), netbuf);
      err_internal_returned = true;
    };
    EthernetNetbuf netbuf(
        eth_netbuf.get(),
        [](void* cookie, zx_status_t status, ethernet_netbuf_t* netbuf) {
          static_cast<decltype(err_internal_callback)*>(cookie)->operator()(status, netbuf);
        },
        &err_internal_callback);

    // The destructor will Return() the buffer with ZX_ERR_INTERNAL.
  }
  EXPECT_TRUE(err_internal_returned);

  // Create an EthernetNetbuf, then explicitly Return() it.
  bool ok_returned = false;
  {
    auto eth_netbuf = std::make_unique<ethernet_netbuf_t>();
    auto err_internal_callback = [&](zx_status_t status, ethernet_netbuf_t* netbuf) {
      EXPECT_EQ(ZX_OK, status);
      EXPECT_EQ(eth_netbuf.get(), netbuf);
      ok_returned = true;
    };
    EthernetNetbuf netbuf(
        eth_netbuf.get(),
        [](void* cookie, zx_status_t status, ethernet_netbuf_t* netbuf) {
          static_cast<decltype(err_internal_callback)*>(cookie)->operator()(status, netbuf);
        },
        &err_internal_callback);
    netbuf.Return(ZX_OK);
  }
  EXPECT_TRUE(ok_returned);
}

TEST(Netbuf, AllocatedNetbuf) {
  static constexpr size_t kAllocationSize = 4096;

  // Create an AllocatedNetbuf, then allow it to automatically Return().
  {
    auto allocation = std::make_unique<char[]>(kAllocationSize);
    AllocatedNetbuf netbuf(std::move(allocation), kAllocationSize);
  }

  // Create an AllocatedNetbuf, then explicitly Return() it.
  {
    auto allocation = std::make_unique<char[]>(kAllocationSize);
    AllocatedNetbuf netbuf(std::move(allocation), kAllocationSize);
    netbuf.Return(ZX_OK);
  }
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan

namespace {

#define SMALL_SIZE (17u)
#define BIG_SIZE (16u * 1024u)

class TestPattern {
 public:
  TestPattern();
  void Set(void* target, size_t len);
  void Check(void* target, size_t len);

 private:
  uint8_t pattern[BIG_SIZE];
};

TestPattern::TestPattern() {
  pattern[0] = 17;
  for (size_t i = 1; i < BIG_SIZE; i++) {
    pattern[i] = (pattern[i - 1] << 1) ^ i;
  }
}

void TestPattern::Set(void* target, size_t len) { memcpy(target, pattern, len); }

void TestPattern::Check(void* target, size_t len) { EXPECT_EQ(memcmp(target, pattern, len), 0); }

static TestPattern testPattern;

class BrcmfNetbuf : public testing::Test {
 public:
  BrcmfNetbuf();
  ~BrcmfNetbuf();
  struct brcmf_netbuf* buf = nullptr;
};

BrcmfNetbuf::BrcmfNetbuf() {
  buf = brcmf_netbuf_allocate(BIG_SIZE);
  EXPECT_NE(buf, nullptr);
}

BrcmfNetbuf::~BrcmfNetbuf() { brcmf_netbuf_free(buf); }

TEST_F(BrcmfNetbuf, CanAllocate) { EXPECT_NE(buf, nullptr); }

TEST_F(BrcmfNetbuf, AllocSizeAligned) {
  struct brcmf_netbuf* buf = brcmf_netbuf_allocate(SMALL_SIZE);
  EXPECT_NE(buf, nullptr);
  EXPECT_EQ(buf->allocated_size, (SMALL_SIZE + 3) & ~3);
  brcmf_netbuf_free(buf);
}

TEST_F(BrcmfNetbuf, HasRightSize) {
  EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE);
  EXPECT_EQ(brcmf_netbuf_head_space(buf), 0u);
  EXPECT_EQ(buf->len, 0u);
}

TEST_F(BrcmfNetbuf, GrowTail) {
  brcmf_netbuf_grow_tail(buf, SMALL_SIZE);
  EXPECT_EQ(buf->len, SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE - SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_head_space(buf), 0u);
  // Growing the tail shouldn't modify data already there.
  testPattern.Set(buf->data, SMALL_SIZE);
  brcmf_netbuf_grow_tail(buf, 2 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  testPattern.Check(buf->data, SMALL_SIZE);
}

TEST_F(BrcmfNetbuf, ShrinkHead) {
  brcmf_netbuf_grow_tail(buf, 2 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 2 * SMALL_SIZE);
  brcmf_netbuf_shrink_head(buf, SMALL_SIZE);
  EXPECT_EQ(buf->len, SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE - 2 * SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_head_space(buf), SMALL_SIZE);
}

TEST_F(BrcmfNetbuf, ShrinkAndGrowHead) {
  brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  testPattern.Set(buf->data, 3 * SMALL_SIZE);
  brcmf_netbuf_shrink_head(buf, 2 * SMALL_SIZE);
  EXPECT_EQ(buf->len, SMALL_SIZE);
  brcmf_netbuf_grow_head(buf, SMALL_SIZE);
  EXPECT_EQ(buf->len, 2 * SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE - 3 * SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_head_space(buf), SMALL_SIZE);
  // All the data should still be there; shrinking doesn't erase it.
  testPattern.Check(buf->data - SMALL_SIZE, 3 * SMALL_SIZE);
}

TEST_F(BrcmfNetbuf, HeadMovePreservesData) {
  brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
  testPattern.Set(buf->data, 3 * SMALL_SIZE);
  brcmf_netbuf_shrink_head(buf, 2 * SMALL_SIZE);
  brcmf_netbuf_grow_head(buf, 2 * SMALL_SIZE);
  testPattern.Check(buf->data, 3 * SMALL_SIZE);
}

TEST_F(BrcmfNetbuf, ReallocHead) {
  brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  testPattern.Set(buf->data, 3 * SMALL_SIZE);
  brcmf_netbuf_grow_realloc(buf, SMALL_SIZE, 0);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_head_space(buf), SMALL_SIZE);
  testPattern.Check(buf->data, 3 * SMALL_SIZE);
}

TEST_F(BrcmfNetbuf, ReallocTail) {
  brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  testPattern.Set(buf->data, 3 * SMALL_SIZE);
  brcmf_netbuf_grow_realloc(buf, 0, SMALL_SIZE);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_head_space(buf), 0u);
  EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE - 2 * SMALL_SIZE);
  testPattern.Check(buf->data, 3 * SMALL_SIZE);
}

TEST_F(BrcmfNetbuf, ReallocBoth) {
  brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  testPattern.Set(buf->data, 3 * SMALL_SIZE);
  brcmf_netbuf_grow_realloc(buf, SMALL_SIZE, 2 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_head_space(buf), SMALL_SIZE);
  EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE - SMALL_SIZE);
  testPattern.Check(buf->data, 3 * SMALL_SIZE);
}

TEST_F(BrcmfNetbuf, SetLength) {
  brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
  brcmf_netbuf_set_length_to(buf, 2 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 2 * SMALL_SIZE);
  brcmf_netbuf_set_length_to(buf, 4 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 4 * SMALL_SIZE);
}

TEST_F(BrcmfNetbuf, ReduceLength) {
  brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
  brcmf_netbuf_reduce_length_to(buf, 4 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
  brcmf_netbuf_reduce_length_to(buf, 2 * SMALL_SIZE);
  EXPECT_EQ(buf->len, 2 * SMALL_SIZE);
}

class BrcmfNetbufList : public testing::Test {
 public:
  BrcmfNetbufList();
  ~BrcmfNetbufList();
  struct brcmf_netbuf* Buf(int32_t tag);
  int32_t Tag(struct brcmf_netbuf* buf) { return *(int32_t*)buf->data; }
  void ExpectOrder(int32_t tags[]);
  struct brcmf_netbuf_list list;
};

// Utility function that returns a tagged brcmf_netbuf
struct brcmf_netbuf* BrcmfNetbufList::Buf(int32_t tag) {
  struct brcmf_netbuf* buf = brcmf_netbuf_allocate(4);
  brcmf_netbuf_grow_tail(buf, 4);
  *(int32_t*)buf->data = tag;
  return buf;
}

void BrcmfNetbufList::ExpectOrder(int32_t* tags) {
  uint32_t i = 0;
  struct brcmf_netbuf* buf;
  brcmf_netbuf_list_for_every(&list, buf) {
    ASSERT_NE(tags[i], -1);
    EXPECT_EQ(tags[i], *(int32_t*)buf->data);
    i++;
  }
  EXPECT_EQ(tags[i], -1);
  EXPECT_EQ(brcmf_netbuf_list_length(&list), i);
  EXPECT_EQ(list.qlen, i);
  EXPECT_EQ(list.qlen, list_length(&list.listnode));
}

BrcmfNetbufList::BrcmfNetbufList() { brcmf_netbuf_list_init(&list); }

BrcmfNetbufList::~BrcmfNetbufList() {
  struct brcmf_netbuf* buf;
  struct brcmf_netbuf* temp;
  EXPECT_EQ(list.qlen, list_length(&list.listnode));
  brcmf_netbuf_list_for_every_safe(&list, buf, temp) { brcmf_netbuf_free(buf); }
}

// It's hard to test length without adding, so I combined the tests.
TEST_F(BrcmfNetbufList, AddHeadAndLength) {
  EXPECT_EQ(brcmf_netbuf_list_length(&list), 0u);
  EXPECT_EQ(brcmf_netbuf_list_is_empty(&list), true);
  brcmf_netbuf_list_add_head(&list, Buf(1));
  EXPECT_EQ(brcmf_netbuf_list_length(&list), 1u);
  EXPECT_EQ(brcmf_netbuf_list_is_empty(&list), false);
  brcmf_netbuf_list_add_head(&list, Buf(2));
  brcmf_netbuf_list_add_head(&list, Buf(3));
  EXPECT_EQ(brcmf_netbuf_list_length(&list), 3u);
  int32_t tags[] = {3, 2, 1, -1};
  ExpectOrder(tags);
}

TEST_F(BrcmfNetbufList, AddTailAndPeek) {
  EXPECT_EQ(brcmf_netbuf_list_peek_head(&list), nullptr);
  EXPECT_EQ(brcmf_netbuf_list_peek_tail(&list), nullptr);
  brcmf_netbuf_list_add_tail(&list, Buf(1));
  brcmf_netbuf_list_add_tail(&list, Buf(2));
  brcmf_netbuf_list_add_tail(&list, Buf(3));
  int32_t tags[] = {1, 2, 3, -1};
  ExpectOrder(tags);
  EXPECT_EQ(Tag(brcmf_netbuf_list_peek_head(&list)), 1);
  EXPECT_EQ(Tag(brcmf_netbuf_list_peek_tail(&list)), 3);
}

TEST_F(BrcmfNetbufList, ListPrev) {
  brcmf_netbuf* buf1 = Buf(1);
  brcmf_netbuf* buf2 = Buf(2);
  brcmf_netbuf_list_add_tail(&list, buf1);
  brcmf_netbuf_list_add_tail(&list, buf2);
  EXPECT_EQ(brcmf_netbuf_list_prev(&list, buf1), nullptr);
  EXPECT_EQ(brcmf_netbuf_list_prev(&list, buf2), buf1);
}

TEST_F(BrcmfNetbufList, PrevAndNext) {
  brcmf_netbuf* buf1 = Buf(1);
  brcmf_netbuf* buf2 = Buf(2);
  brcmf_netbuf* buf3 = Buf(3);
  brcmf_netbuf_list_add_tail(&list, buf1);
  brcmf_netbuf_list_add_tail(&list, buf2);
  brcmf_netbuf_list_add_tail(&list, buf3);
  EXPECT_EQ(brcmf_netbuf_list_prev(&list, buf2), buf1);
  EXPECT_EQ(brcmf_netbuf_list_next(&list, buf2), buf3);
}

TEST_F(BrcmfNetbufList, RemoveTail) {
  EXPECT_EQ(brcmf_netbuf_list_remove_tail(&list), nullptr);
  brcmf_netbuf* buf1 = Buf(1);
  brcmf_netbuf_list_add_tail(&list, buf1);
  EXPECT_EQ(brcmf_netbuf_list_remove_tail(&list), buf1);
  EXPECT_EQ(brcmf_netbuf_list_is_empty(&list), true);
  brcmf_netbuf* buf2 = Buf(2);
  brcmf_netbuf_list_add_tail(&list, buf1);
  brcmf_netbuf_list_add_tail(&list, buf2);
  int32_t tags[] = {1, 2, -1};
  ExpectOrder(tags);
  EXPECT_EQ(brcmf_netbuf_list_remove_tail(&list), buf2);
  int32_t tags2[] = {1, -1};
  ExpectOrder(tags2);
}

TEST_F(BrcmfNetbufList, RemoveHead) {
  EXPECT_EQ(brcmf_netbuf_list_remove_head(&list), nullptr);
  brcmf_netbuf* buf1 = Buf(1);
  brcmf_netbuf_list_add_tail(&list, buf1);
  EXPECT_EQ(brcmf_netbuf_list_remove_head(&list), buf1);
  EXPECT_EQ(brcmf_netbuf_list_is_empty(&list), true);
  brcmf_netbuf* buf2 = Buf(2);
  brcmf_netbuf_list_add_tail(&list, buf1);
  brcmf_netbuf_list_add_tail(&list, buf2);
  int32_t tags[] = {1, 2, -1};
  ExpectOrder(tags);
  EXPECT_EQ(brcmf_netbuf_list_remove_head(&list), buf1);
  int32_t tags2[] = {2, -1};
  ExpectOrder(tags2);
}

TEST_F(BrcmfNetbufList, Remove) {
  brcmf_netbuf* buf1 = Buf(1);
  brcmf_netbuf* buf2 = Buf(2);
  brcmf_netbuf* buf3 = Buf(3);
  brcmf_netbuf_list_add_tail(&list, buf1);
  brcmf_netbuf_list_remove(&list, buf1);
  EXPECT_EQ(brcmf_netbuf_list_is_empty(&list), true);
  brcmf_netbuf_list_add_tail(&list, buf1);
  brcmf_netbuf_list_add_tail(&list, buf2);
  int32_t tags[] = {1, 2, -1};
  ExpectOrder(tags);
  brcmf_netbuf_list_remove(&list, buf1);
  int32_t tags1[] = {2, -1};
  ExpectOrder(tags1);
  brcmf_netbuf_list_add_head(&list, buf1);
  brcmf_netbuf_list_remove(&list, buf2);
  int32_t tags2[] = {1, -1};
  ExpectOrder(tags2);
  brcmf_netbuf_list_add_tail(&list, buf2);
  brcmf_netbuf_list_add_tail(&list, buf3);
  brcmf_netbuf_list_remove(&list, buf2);
  int32_t tags3[] = {1, 3, -1};
  ExpectOrder(tags3);
}

TEST_F(BrcmfNetbufList, AddAfter) {
  brcmf_netbuf* buf1 = Buf(1);
  brcmf_netbuf* buf2 = Buf(2);
  brcmf_netbuf* buf3 = Buf(3);
  brcmf_netbuf_list_add_tail(&list, buf1);
  brcmf_netbuf_list_add_after(&list, buf1, buf3);
  brcmf_netbuf_list_add_after(&list, buf1, buf2);
  int32_t tags[] = {1, 2, 3, -1};
  ExpectOrder(tags);
  EXPECT_EQ(brcmf_netbuf_list_peek_tail(&list), buf3);
}

}  // namespace
