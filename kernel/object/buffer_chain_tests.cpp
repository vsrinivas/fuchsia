// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/buffer_chain.h>

#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>
#include <lib/user_copy/user_ptr.h>
#include <stdio.h>

namespace {

using testing::UserMemory;

static bool alloc_free_basic() {
    BEGIN_TEST;

    // An empty chain requires one buffer
    BufferChain* bc = BufferChain::Alloc(0);
    ASSERT_NE(bc, nullptr, "");
    ASSERT_FALSE(bc->buffers()->is_empty(), "");
    ASSERT_EQ(bc->buffers()->size_slow(), 1u, "");
    BufferChain::Free(bc);

    // One Buffer is enough to hold one byte.
    bc = BufferChain::Alloc(1);
    ASSERT_FALSE(bc->buffers()->is_empty(), "");
    ASSERT_EQ(bc->buffers()->size_slow(), 1u, "");
    ASSERT_NE(bc, nullptr, "");
    BufferChain::Free(bc);

    // One Buffer is still enough.
    bc = BufferChain::Alloc(BufferChain::kContig);
    ASSERT_FALSE(bc->buffers()->is_empty(), "");
    ASSERT_EQ(bc->buffers()->size_slow(), 1u, "");
    ASSERT_NE(bc, nullptr, "");
    BufferChain::Free(bc);

    // Two Buffers required.
    bc = BufferChain::Alloc(BufferChain::kContig + 1);
    ASSERT_FALSE(bc->buffers()->is_empty(), "");
    ASSERT_EQ(bc->buffers()->size_slow(), 2u, "");
    ASSERT_NE(bc, nullptr, "");
    BufferChain::Free(bc);

    // Many Buffers required.
    bc = BufferChain::Alloc(10000 * BufferChain::kRawDataSize);
    ASSERT_FALSE(bc->buffers()->is_empty(), "");
    ASSERT_EQ(bc->buffers()->size_slow(), 1u + 10000u, "");
    ASSERT_NE(bc, nullptr, "");
    BufferChain::Free(bc);

    END_TEST;
}

static bool copy_in_copy_out() {
    BEGIN_TEST;

    constexpr size_t kSize = BufferChain::kContig + 2 * BufferChain::kRawDataSize;
    fbl::AllocChecker ac;
    auto buf = fbl::unique_ptr<char[]>(new (&ac) char[kSize]);
    ASSERT_TRUE(ac.check(), "");
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(kSize);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    BufferChain* bc = BufferChain::Alloc(kSize);
    ASSERT_NE(nullptr, bc, "");
    ASSERT_FALSE(bc->buffers()->is_empty(), "");

    // Fill the chain with 'A'.
    memset(buf.get(), 'A', kSize);
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kSize), "");
    ASSERT_EQ(ZX_OK, bc->CopyIn(mem_in, 0, kSize), "");

    // Verify it.
    ASSERT_EQ(3u, bc->buffers()->size_slow(), "");
    for (auto& b : *bc->buffers()) {
        char* data = b.data();
        for (size_t i = 0; i < b.size(); ++i) {
            ASSERT_EQ('A', data[i], "");
        }
    }

    // Write a chunk of 'B' straddling all three buffers.
    memset(buf.get(), 'B', kSize);
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kSize), "");
    size_t offset = BufferChain::kContig - 1;
    size_t size = BufferChain::kRawDataSize + 2;
    ASSERT_EQ(ZX_OK, bc->CopyIn(mem_in, offset, size), "");

    // Verify it.
    auto iter = bc->buffers()->begin();
    for (size_t i = 0; i < offset; ++i) {
        char* data = iter->data();
        ASSERT_EQ('A', data[i], "");
    }
    ASSERT_EQ('B', *(iter->data() + offset), "");
    ++iter;
    for (size_t i = 0; i < BufferChain::kRawDataSize; ++i) {
        char* data = iter->data();
        ASSERT_EQ('B', data[i], "");
    }
    ++iter;
    ASSERT_EQ('B', *iter->data(), "");
    for (size_t i = 1; i < BufferChain::kRawDataSize; ++i) {
        char* data = iter->data();
        EXPECT_EQ('A', data[i], "");
    }
    ASSERT_TRUE(++iter == bc->buffers()->end(), "");

    // Copy it all out.
    memset(buf.get(), 0, kSize);
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kSize), "");
    ASSERT_EQ(ZX_OK, bc->CopyOut(mem_out, 0, kSize), "");

    // Verify it.
    memset(buf.get(), 0, kSize);
    ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(buf.get(), kSize), "");
    size_t index = 0;
    for (size_t i = 0; i < offset; ++i) {
        ASSERT_EQ('A', buf[index++], "");
    }
    EXPECT_EQ('B', buf[index++], "");
    for (size_t i = 0; i < BufferChain::kRawDataSize; ++i) {
        ASSERT_EQ('B', buf[index++], "");
    }
    ASSERT_EQ('B', buf[index++], "");
    for (size_t i = 1; i < BufferChain::kRawDataSize; ++i) {
        EXPECT_EQ('A', buf[index++], "");
    }

    BufferChain::Free(bc);

    END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(buffer_chain_tests)
UNITTEST("alloc_free_basic", alloc_free_basic)
UNITTEST("copy_in_copy_out", copy_in_copy_out)
UNITTEST_END_TESTCASE(buffer_chain_tests, "buffer_chain", "BufferChain tests");
