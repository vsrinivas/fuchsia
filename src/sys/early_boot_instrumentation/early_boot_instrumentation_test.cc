// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/stdcompat/array.h>

#include <array>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace {
template <size_t N>
bool CheckContents(const char (&str)[N], fbl::unique_fd file) {
  auto expected = cpp20::to_array(str);
  decltype(expected) actual = {};
  size_t acc_offset = 0;
  while (auto read_bytes =
             read(file.get(), actual.data() + acc_offset, actual.size() - acc_offset)) {
    acc_offset += read_bytes;
  }
  return acc_offset == expected.size() &&
         memcmp(actual.data(), expected.data(), actual.size()) == 0;
}

}  // namespace

TEST(EarlyBootInstrumentationTest, HasKernelInDynamic) {
  fbl::unique_fd kernel_file(open("/profraw/dynamic/zircon.profraw", O_RDONLY));
  ASSERT_TRUE(kernel_file);
  ASSERT_TRUE(CheckContents("kernel", std::move(kernel_file)));
}

TEST(EarlyBootInstrumentationTest, HasPhysbootInStatic) {
  fbl::unique_fd physboot_file(open("/profraw/static/physboot.profraw", O_RDONLY));
  ASSERT_TRUE(physboot_file);
  ASSERT_TRUE(CheckContents("physboot", std::move(physboot_file)));
}

TEST(EarlyBootInstrumentationTest, HasSvcStashData) {
  fbl::unique_fd static_file(open("/profraw/static/0-0.profraw", O_RDONLY));
  ASSERT_TRUE(static_file);
  ASSERT_TRUE(CheckContents("1234", std::move(static_file)));

  fbl::unique_fd dynamic_file(open("/profraw/dynamic/0-1.profraw", O_RDONLY));
  ASSERT_TRUE(dynamic_file);
  ASSERT_TRUE(CheckContents("567890123", std::move(dynamic_file)));
}
