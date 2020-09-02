// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "llvm-fuzzer.h"

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <stddef.h>
#include <stdint.h>

#include <gtest/gtest.h>

#include "data-provider.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Test fixture

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int total = 0;
  for (size_t i = 0; i < size; ++i) {
    total += data[i];
  }
  return total;
}

namespace fuzzing {

////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit test

TEST(LlvmFuzzerTest, Initialize) {
  LlvmFuzzerImpl llvm_fuzzer;
  TestInput input;
  EXPECT_EQ(input.Create(), ZX_OK);

  // Bad test input.
  zx::vmo vmo;
  std::vector<std::string> options;
  std::vector<std::string> modified;
  zx_status_t status = ZX_OK;
  llvm_fuzzer.Initialize(std::move(vmo), std::vector<std::string>(options),
                         [&status, &modified](zx_status_t rc, std::vector<std::string> results) {
                           status = rc;
                           modified = std::move(results);
                         });
  EXPECT_EQ(status, ZX_ERR_BAD_HANDLE);

  // Valid, options empty.
  EXPECT_EQ(input.Share(&vmo), ZX_OK);
  llvm_fuzzer.Initialize(std::move(vmo), std::vector<std::string>(options),
                         [&status, &modified](zx_status_t rc, std::vector<std::string> results) {
                           status = rc;
                           modified = std::move(results);
                         });
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(modified, options);

  // Already initialized.
  EXPECT_EQ(input.Share(&vmo), ZX_OK);
  llvm_fuzzer.Initialize(std::move(vmo), std::vector<std::string>(options),
                         [&status, &modified](zx_status_t rc, std::vector<std::string> results) {
                           status = rc;
                           modified = std::move(results);
                         });
  EXPECT_EQ(status, ZX_ERR_BAD_STATE);

  // Valid, options non-empty.
  llvm_fuzzer.Reset();
  options.push_back("-seed=1337");
  options.push_back("-runs=1000");
  EXPECT_EQ(input.Share(&vmo), ZX_OK);
  llvm_fuzzer.Initialize(std::move(vmo), std::vector<std::string>(options),
                         [&status, &modified](zx_status_t rc, std::vector<std::string> results) {
                           status = rc;
                           modified = std::move(results);
                         });
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(modified, options);
}

TEST(LlvmFuzzerTest, TestOneInput) {
  DataProviderImpl data_provider;
  LlvmFuzzerImpl llvm_fuzzer;

  zx::vmo vmo;
  EXPECT_EQ(data_provider.Initialize(&vmo), ZX_OK);
  llvm_fuzzer.Initialize(std::move(vmo), std::vector<std::string>(),
                         [](zx_status_t rc, std::vector<std::string> results) {});

  // TestOneInput works without data.
  std::string data;
  EXPECT_EQ(data_provider.PartitionTestInput(data.c_str(), data.size()), ZX_OK);

  int result = -1;
  llvm_fuzzer.TestOneInput([&result](int rc) { result = rc; });
  EXPECT_EQ(result, 0);

  // TestOneInput works with data
  data = "ABCD";
  EXPECT_EQ(data_provider.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  llvm_fuzzer.TestOneInput([&result](int rc) { result = rc; });
  EXPECT_EQ(result, 266 /* 0x41 + 0x42 + 0x43 + 0x44*/);
}

}  // namespace fuzzing
