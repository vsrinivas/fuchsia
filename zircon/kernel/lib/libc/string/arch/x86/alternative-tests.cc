// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <memory>

#include <gtest/gtest.h>

extern "C" void* memcpy_movsb(void* dest, const void* src, size_t count);
extern "C" void* memcpy_movsq(void* dest, const void* src, size_t count);
extern "C" void* memset_stosb(void* s, int c, size_t count);
extern "C" void* memset_stosq(void* s, int c, size_t count);

namespace {

using MemcpyFunc = void* (*)(void*, const void*, size_t);
using MemsetFunc = void* (*)(void*, int, size_t);

void TestMemcpy(MemcpyFunc memcpy_func) {
  for (size_t i = 0; i < 40; ++i) {
    auto dst = std::make_unique<uint8_t[]>(i);       // Zero-filled.
    std::unique_ptr<uint8_t[]> src(new uint8_t[i]);  // Uninitialized array, immediately filled.
    for (size_t j = 0; j < i; ++j) {
      src[j] = static_cast<uint8_t>(i);
    }

    auto result = memcpy_func(dst.get(), src.get(), i);
    EXPECT_EQ(dst.get(), static_cast<uint8_t*>(result));
    for (size_t j = 0; j < i; ++j) {
      EXPECT_EQ(src[j], dst[j]) << "case (" << i << ", " << j << ")";
    }
  }
}

void TestMemset(MemsetFunc memset_func) {
  for (size_t i = 0; i < 40; ++i) {
    auto buff = std::make_unique<uint8_t[]>(i);  // Zero-filled.
    auto result = memset_func(buff.get(), static_cast<int>(i), i);
    EXPECT_EQ(buff.get(), static_cast<uint8_t*>(result));
    for (size_t j = 0; j < i; ++j) {
      EXPECT_EQ(i, buff[j]) << "case (" << i << ", " << j << ")";
    }
  }
}

TEST(X86CstringTests, memcpy_movsb) { ASSERT_NO_FATAL_FAILURE(TestMemcpy(memcpy_movsb)); }

TEST(X86CstringTests, memcpy_movsq) { ASSERT_NO_FATAL_FAILURE(TestMemcpy(memcpy_movsq)); }

TEST(X86CstringTests, memset_stosb) { ASSERT_NO_FATAL_FAILURE(TestMemset(memset_stosb)); }

TEST(X86CstringTests, memset_stosq) { ASSERT_NO_FATAL_FAILURE(TestMemset(memset_stosq)); }

}  // namespace
