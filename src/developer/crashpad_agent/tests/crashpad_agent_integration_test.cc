// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/errors.h>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace crash {
namespace {

// Smoke-tests the real environment service for the fuchsia.crash.Analyzer FIDL
// interface, connecting through FIDL.
TEST(CrashpadAgentIntegrationTest, SmokeTest) {
  AnalyzerSyncPtr crash_analyzer;
  auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(crash_analyzer.NewRequest());

  // We call OnKernelPanicCrashLog() to smoke test the service is up and
  // running because it is the easiest to call.
  ::fuchsia::mem::Buffer crash_log;
  ASSERT_TRUE(::fsl::VmoFromString("ZIRCON KERNEL PANIC", &crash_log));
  Analyzer_OnKernelPanicCrashLog_Result out_result;
  ASSERT_EQ(
      crash_analyzer->OnKernelPanicCrashLog(std::move(crash_log), &out_result),
      ZX_OK);
  EXPECT_TRUE(out_result.is_response());
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia
