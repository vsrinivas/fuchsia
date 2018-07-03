/*
 * Copyright (c) 2018 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

extern "C" {
#include <stdio.h>
#include "netbuf.h"

bool error_happened;

void __brcmf_err(const char* func, const char* fmt, ...) {
    error_happened = true;
}

void __brcmf_dbg(uint32_t filter, const char* func, const char* fmt, ...) {}

} // extern "C"

#include "gtest/gtest.h"

namespace {

#define SMALL_SIZE (17u)
#define BIG_SIZE (16u*1024u)

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
        pattern[i] = (pattern[i-1] << 1) ^ i;
    }
}

void TestPattern::Set(void* target, size_t len) {
    memcpy(target, pattern, len);
}

void TestPattern::Check(void* target, size_t len) {
    EXPECT_EQ(memcmp(target, pattern, len), 0);
}

static TestPattern testPattern;

class Netbuf : public testing::Test {
public:
    Netbuf();
    ~Netbuf();
    struct brcmf_netbuf* buf = nullptr;
};

Netbuf::Netbuf() {
    error_happened = false;
    buf = brcmf_netbuf_allocate(BIG_SIZE);
    EXPECT_NE(buf, nullptr);
}

Netbuf::~Netbuf() {
    brcmf_netbuf_free(buf);
    EXPECT_EQ(error_happened, false);
}

TEST_F(Netbuf, CanAllocate) {
    EXPECT_NE(buf, nullptr);
    EXPECT_EQ(error_happened, false);
}

TEST_F(Netbuf, HasRightSize) {
    EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE);
    EXPECT_EQ(brcmf_netbuf_head_space(buf), 0u);
    EXPECT_EQ(buf->len, 0u);
}

TEST_F(Netbuf, GrowTail) {
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

TEST_F(Netbuf, ShrinkHead) {
    brcmf_netbuf_grow_tail(buf, 2 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 2 * SMALL_SIZE);
    brcmf_netbuf_shrink_head(buf, SMALL_SIZE);
    EXPECT_EQ(buf->len, SMALL_SIZE);
    EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE - 2 * SMALL_SIZE);
    EXPECT_EQ(brcmf_netbuf_head_space(buf), SMALL_SIZE);
}

TEST_F(Netbuf, ShrinkAndGrowHead) {
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

TEST_F(Netbuf, HeadMovePreservesData) {
    brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
    testPattern.Set(buf->data, 3 * SMALL_SIZE);
    brcmf_netbuf_shrink_head(buf, 2 * SMALL_SIZE);
    brcmf_netbuf_grow_head(buf, 2 * SMALL_SIZE);
    testPattern.Check(buf->data, 3 * SMALL_SIZE);
}

TEST_F(Netbuf, ReallocHead) {
    brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
    testPattern.Set(buf->data, 3 * SMALL_SIZE);
    brcmf_netbuf_grow_realloc(buf, SMALL_SIZE, 0);
    EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
    EXPECT_EQ(brcmf_netbuf_head_space(buf), SMALL_SIZE);
    testPattern.Check(buf->data, 3 * SMALL_SIZE);
}

TEST_F(Netbuf, ReallocTail) {
    brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
    testPattern.Set(buf->data, 3 * SMALL_SIZE);
    brcmf_netbuf_grow_realloc(buf, 0, SMALL_SIZE);
    EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
    EXPECT_EQ(brcmf_netbuf_head_space(buf), 0u);
    EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE - 2 * SMALL_SIZE);
    testPattern.Check(buf->data, 3 * SMALL_SIZE);
}

TEST_F(Netbuf, ReallocBoth) {
    brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
    testPattern.Set(buf->data, 3 * SMALL_SIZE);
    brcmf_netbuf_grow_realloc(buf, SMALL_SIZE, 2 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
    EXPECT_EQ(brcmf_netbuf_head_space(buf), SMALL_SIZE);
    EXPECT_EQ(brcmf_netbuf_tail_space(buf), BIG_SIZE - SMALL_SIZE);
    testPattern.Check(buf->data, 3 * SMALL_SIZE);
}

TEST_F(Netbuf, SetLength) {
    brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
    brcmf_netbuf_set_length_to(buf, 2 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 2 * SMALL_SIZE);
    brcmf_netbuf_set_length_to(buf, 4 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 4 * SMALL_SIZE);
}

TEST_F(Netbuf, ReduceLength) {
    brcmf_netbuf_grow_tail(buf, 3 * SMALL_SIZE);
    brcmf_netbuf_reduce_length_to(buf, 4 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 3 * SMALL_SIZE);
    brcmf_netbuf_reduce_length_to(buf, 2 * SMALL_SIZE);
    EXPECT_EQ(buf->len, 2 * SMALL_SIZE);
}

class NetbufList : public testing::Test {
public:
    NetbufList();
    ~NetbufList();
    struct brcmf_netbuf* Buf(int32_t tag);
    int32_t Tag(struct brcmf_netbuf* buf) { return *(int32_t*)buf->data; }
    void ExpectOrder(int32_t tags[]);
    struct brcmf_netbuf_list list;
};

// Utility function that returns a tagged Netbuf
struct brcmf_netbuf* NetbufList::Buf(int32_t tag) {
    struct brcmf_netbuf* buf = brcmf_netbuf_allocate(4);
    brcmf_netbuf_grow_tail(buf, 4);
    *(int32_t*)buf->data = tag;
    return buf;
}

void NetbufList::ExpectOrder(int32_t* tags) {
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

NetbufList::NetbufList() {
    error_happened = false;
    brcmf_netbuf_list_init(&list);
}

NetbufList::~NetbufList() {
    struct brcmf_netbuf* buf;
    struct brcmf_netbuf* temp;
    EXPECT_EQ(list.qlen, list_length(&list.listnode));
    brcmf_netbuf_list_for_every_safe(&list, buf, temp) {
        brcmf_netbuf_free(buf);
    }
    EXPECT_EQ(error_happened, false);
}

// It's hard to test length without adding, so I combined the tests.
TEST_F(NetbufList, AddHeadAndLength) {
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

TEST_F(NetbufList, AddTailAndPeek) {
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

TEST_F(NetbufList, ListPrev) {
    brcmf_netbuf* buf1 = Buf(1);
    brcmf_netbuf* buf2 = Buf(2);
    brcmf_netbuf_list_add_tail(&list, buf1);
    brcmf_netbuf_list_add_tail(&list, buf2);
    EXPECT_EQ(brcmf_netbuf_list_prev(&list, buf1), nullptr);
    EXPECT_EQ(brcmf_netbuf_list_prev(&list, buf2), buf1);
}

TEST_F(NetbufList, PrevAndNext) {
    brcmf_netbuf* buf1 = Buf(1);
    brcmf_netbuf* buf2 = Buf(2);
    brcmf_netbuf* buf3 = Buf(3);
    brcmf_netbuf_list_add_tail(&list, buf1);
    brcmf_netbuf_list_add_tail(&list, buf2);
    brcmf_netbuf_list_add_tail(&list, buf3);
    EXPECT_EQ(brcmf_netbuf_list_prev(&list, buf2), buf1);
    EXPECT_EQ(brcmf_netbuf_list_next(&list, buf2), buf3);
}

TEST_F(NetbufList, RemoveTail) {
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

TEST_F(NetbufList, RemoveHead) {
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

TEST_F(NetbufList, Remove) {
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

TEST_F(NetbufList, AddAfter) {
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
