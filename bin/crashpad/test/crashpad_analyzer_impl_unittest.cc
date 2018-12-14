// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/crashpad/crashpad_analyzer_impl.h"

#include <utility>

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/files/scoped_temp_dir.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

namespace fuchsia {
namespace crash {
namespace {

class CrashpadAnalyzerImplTest : public ::testing::Test {
 public:
  void SetUp() override {
    analyzer_ = CrashpadAnalyzerImpl::TryCreate(database_path_.path());
  }

 protected:
  std::unique_ptr<CrashpadAnalyzerImpl> analyzer_;

 private:
  files::ScopedTempDir database_path_;
};

TEST_F(CrashpadAnalyzerImplTest, HandleManagedRuntimeException_Dart_Basic) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::DART, "component_url", "UnhandledException: Foo",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
}

TEST_F(CrashpadAnalyzerImplTest,
       HandleManagedRuntimeException_Dart_ExceptionStringInBadFormat) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::DART, "component_url", "wrong format",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
}

TEST_F(CrashpadAnalyzerImplTest,
       HandleManagedRuntimeException_OtherLanguage_Basic) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::OTHER_LANGUAGE, "component_url", "error",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
}

TEST_F(CrashpadAnalyzerImplTest, ProcessKernelPanicCrashlog_Basic) {
  fuchsia::mem::Buffer crashlog;
  ASSERT_TRUE(fsl::VmoFromString("ZIRCON KERNEL PANIC", &crashlog));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->ProcessKernelPanicCrashlog(
      std::move(crashlog),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}
