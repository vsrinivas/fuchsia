// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <gtest/gtest.h>

#ifndef FUNCTION_NAME
#error "FUNCTION_NAME not defined"
#endif

struct X64CopyToFromUserRet {
  zx_status_t status;
  uint pf_flags;
  zx_vaddr_t pf_va;
};

extern "C" X64CopyToFromUserRet FUNCTION_NAME(void* dst, const void* src, size_t len,
                                              uint64_t* fault_return, uint64_t fault_return_mask);
namespace {

TEST(X86UserCopyTests, FUNCTION_NAME) {
  for (size_t i = 1; i < 40; ++i) {
    auto dst = std::make_unique<uint8_t[]>(i);
    std::unique_ptr<uint8_t[]> src(new uint8_t[i]);
    for (size_t j = 0; j < i; ++j) {
      src[j] = static_cast<uint8_t>(i);
    }

    uint64_t fault_return = 0;
    auto result = FUNCTION_NAME(dst.get(), src.get(), i, &fault_return, 0);
    EXPECT_EQ(ZX_OK, result.status);
    for (size_t j = 0; j < i; ++j) {
      EXPECT_EQ(src[j], dst[j]) << "case (" << i << ", " << j << ")";
    }

    // The fault return address should have been reset.
    EXPECT_EQ(0u, fault_return);
  }
}

}  // namespace
