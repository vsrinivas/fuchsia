// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>
#include <lib/user_copy/user_ptr.h>
#include <stdio.h>

#include <fbl/auto_call.h>

#include "object/buffer_chain.h"

namespace {

using testing::UserMemory;

static bool alloc_free_basic() {
  BEGIN_TEST;

  // An empty chain requires one buffer
  BufferChain* bc = BufferChain::Alloc(0);
  ASSERT_NE(bc, nullptr);
  ASSERT_FALSE(bc->buffers()->is_empty());
  ASSERT_EQ(bc->buffers()->size_slow(), 1u);
  BufferChain::Free(bc);

  // One Buffer is enough to hold one byte.
  bc = BufferChain::Alloc(1);
  ASSERT_FALSE(bc->buffers()->is_empty());
  ASSERT_EQ(bc->buffers()->size_slow(), 1u);
  ASSERT_NE(bc, nullptr);
  BufferChain::Free(bc);

  // One Buffer is still enough.
  bc = BufferChain::Alloc(BufferChain::kContig);
  ASSERT_FALSE(bc->buffers()->is_empty());
  ASSERT_EQ(bc->buffers()->size_slow(), 1u);
  ASSERT_NE(bc, nullptr);
  BufferChain::Free(bc);

  // Two pages allocated, only one used for the buffer.
  bc = BufferChain::Alloc(BufferChain::kContig + 1);
  ASSERT_FALSE(bc->buffers()->is_empty());
  ASSERT_EQ(bc->buffers()->size_slow(), 1u);
  ASSERT_NE(bc, nullptr);
  BufferChain::Free(bc);

  // Several pages allocated, only one used for the buffer.
  bc = BufferChain::Alloc(10000 * BufferChain::kRawDataSize);
  ASSERT_FALSE(bc->buffers()->is_empty());
  ASSERT_EQ(bc->buffers()->size_slow(), 1u);
  ASSERT_NE(bc, nullptr);
  BufferChain::Free(bc);

  END_TEST;
}

static bool append_copy_out() {
  BEGIN_TEST;

  constexpr size_t kOffset = 24;
  constexpr size_t kFirstCopy = BufferChain::kContig + 8;
  constexpr size_t kSecondCopy = BufferChain::kRawDataSize + 16;
  constexpr size_t kSize = kOffset + kFirstCopy + kSecondCopy;

  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<char[]>(new (&ac) char[kSize]);
  ASSERT_TRUE(ac.check());
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kSize);
  auto mem_in = mem->user_in<char>();
  auto mem_out = mem->user_out<char>();

  BufferChain* bc = BufferChain::Alloc(kSize);
  ASSERT_NE(nullptr, bc);
  auto free_bc = fbl::MakeAutoCall([&bc]() { BufferChain::Free(bc); });
  ASSERT_EQ(1u, bc->buffers()->size_slow());

  bc->Skip(kOffset);

  // Fill the chain with 'A'.
  memset(buf.get(), 'A', kFirstCopy);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kFirstCopy));
  ASSERT_EQ(ZX_OK, bc->Append(mem_in, kFirstCopy));

  // Verify it.
  auto iter = bc->buffers()->begin();
  for (size_t i = kOffset; i < BufferChain::kContig; ++i) {
    ASSERT_EQ('A', iter->data()[i]);
  }
  ++iter;
  for (size_t i = 0; i < kOffset + kFirstCopy - BufferChain::kContig; ++i) {
    ASSERT_EQ('A', iter->data()[i]);
  }

  // Write a chunk of 'B' straddling all three buffers.
  memset(buf.get(), 'B', kSecondCopy);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kSecondCopy));
  ASSERT_EQ(ZX_OK, bc->Append(mem_in, kSecondCopy));

  // Verify it.
  iter = bc->buffers()->begin();
  for (size_t i = kOffset; i < BufferChain::kContig; ++i) {
    ASSERT_EQ('A', iter->data()[i]);
  }
  ++iter;
  for (size_t i = 0; i < kOffset + kFirstCopy - BufferChain::kContig; ++i) {
    ASSERT_EQ('A', iter->data()[i]);
  }
  for (size_t i = kOffset + kFirstCopy - BufferChain::kContig; i < BufferChain::kRawDataSize; ++i) {
    ASSERT_EQ('B', iter->data()[i]);
  }
  ++iter;
  for (size_t i = 0;
       i < kOffset + kFirstCopy + kSecondCopy - BufferChain::kContig - BufferChain::kRawDataSize;
       ++i) {
    if (iter->data()[i] != 'B') {
      ASSERT_EQ(int(i), -1);
    }
    ASSERT_EQ('B', iter->data()[i]);
  }
  ASSERT_TRUE(++iter == bc->buffers()->end());

  // Copy it all out.
  memset(buf.get(), 0, kSize);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kSize));
  ASSERT_EQ(ZX_OK, bc->CopyOut(mem_out, 0, kSize));

  // Verify it.
  memset(buf.get(), 0, kSize);
  ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(buf.get(), kSize));
  size_t index = kOffset;
  for (size_t i = 0; i < kFirstCopy; ++i) {
    ASSERT_EQ('A', buf[index++]);
  }
  for (size_t i = 0; i < kSecondCopy; ++i) {
    ASSERT_EQ('B', buf[index++]);
  }

  END_TEST;
}

static bool free_unused_pages() {
  BEGIN_TEST;

  constexpr size_t kSize = 8 * PAGE_SIZE;
  constexpr size_t kWriteSize = BufferChain::kContig + 1;

  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<char[]>(new (&ac) char[kWriteSize]);
  ASSERT_TRUE(ac.check());
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteSize);
  auto mem_in = mem->user_in<char>();
  auto mem_out = mem->user_out<char>();

  BufferChain* bc = BufferChain::Alloc(kSize);
  ASSERT_NE(nullptr, bc);
  auto free_bc = fbl::MakeAutoCall([&bc]() { BufferChain::Free(bc); });
  ASSERT_EQ(1u, bc->buffers()->size_slow());

  memset(buf.get(), 0, kWriteSize);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kWriteSize));
  ASSERT_EQ(ZX_OK, bc->Append(mem_in, kWriteSize));

  ASSERT_EQ(2u, bc->buffers()->size_slow());
  bc->FreeUnusedBuffers();
  ASSERT_EQ(2u, bc->buffers()->size_slow());

  END_TEST;
}

static bool append_more_than_allocated() {
  BEGIN_TEST;

  constexpr size_t kAllocSize = 2 * PAGE_SIZE;
  constexpr size_t kWriteSize = 2 * kAllocSize;

  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<char[]>(new (&ac) char[kWriteSize]);
  ASSERT_TRUE(ac.check());
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteSize);
  auto mem_in = mem->user_in<char>();
  auto mem_out = mem->user_out<char>();

  BufferChain* bc = BufferChain::Alloc(kAllocSize);
  ASSERT_NE(nullptr, bc);
  auto free_bc = fbl::MakeAutoCall([&bc]() { BufferChain::Free(bc); });
  ASSERT_EQ(1u, bc->buffers()->size_slow());

  memset(buf.get(), 0, kWriteSize);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kWriteSize));
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, bc->Append(mem_in, kWriteSize));

  END_TEST;
}

static bool append_after_fail_fails() {
  BEGIN_TEST;

  constexpr size_t kAllocSize = 2 * PAGE_SIZE;
  constexpr size_t kWriteSize = PAGE_SIZE;

  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<char[]>(new (&ac) char[kWriteSize]);
  ASSERT_TRUE(ac.check());
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteSize);
  auto mem_in = mem->user_in<char>();
  auto mem_out = mem->user_out<char>();

  BufferChain* bc = BufferChain::Alloc(kAllocSize);
  ASSERT_NE(nullptr, bc);
  auto free_bc = fbl::MakeAutoCall([&bc]() { BufferChain::Free(bc); });
  ASSERT_EQ(1u, bc->buffers()->size_slow());

  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            bc->Append(make_user_in_ptr(static_cast<const char*>(nullptr)), kWriteSize));

  memset(buf.get(), 0, kWriteSize);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kWriteSize));
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, bc->Append(mem_in, kWriteSize));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(buffer_chain_tests)
UNITTEST("alloc_free_basic", alloc_free_basic)
UNITTEST("append_copy_out", append_copy_out)
UNITTEST("free_unused_pages", free_unused_pages)
UNITTEST("append_more_than_allocated", append_more_than_allocated)
UNITTEST("append_after_fail_fails", append_after_fail_fails)
UNITTEST_END_TESTCASE(buffer_chain_tests, "buffer_chain", "BufferChain tests")
