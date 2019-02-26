// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

namespace fuchsia {
namespace crash {
namespace {

// Smoke-tests the real environment service for the fuchsia.crash.Analyzer FIDL
// interface, connecting through FIDL.
TEST(CrashpadAnalyzerIntegrationTest, SmokeTest) {
  AnalyzerSyncPtr crash_analyzer;
  auto environment_services = sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(crash_analyzer.NewRequest());

  // We call ProcessKernelPanicCrashlog() to smoke test the service is up and
  // running because it is the easiest to call.
  ::fuchsia::mem::Buffer crashlog;
  ASSERT_TRUE(::fsl::VmoFromString("ZIRCON KERNEL PANIC", &crashlog));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  ASSERT_EQ(crash_analyzer->ProcessKernelPanicCrashlog(std::move(crashlog),
                                                       &out_status),
            ZX_OK);
  EXPECT_EQ(out_status, ZX_OK);
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}
