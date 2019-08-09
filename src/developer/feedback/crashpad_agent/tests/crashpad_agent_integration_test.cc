// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/errors.h>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace crash {
namespace {

// Smoke-tests the actual service for fuchsia.crash.Analyzer, connecting through FIDL.
TEST(CrashpadAgentIntegrationTest, CrashAnalyzer_SmokeTest) {
  AnalyzerSyncPtr crash_analyzer;
  auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(crash_analyzer.NewRequest());

  // We call OnKernelPanicCrashLog() to smoke test the service is up and running because it is the
  // easiest to call.
  ::fuchsia::mem::Buffer crash_log;
  ASSERT_TRUE(::fsl::VmoFromString("ZIRCON KERNEL PANIC", &crash_log));
  Analyzer_OnKernelPanicCrashLog_Result out_result;
  ASSERT_EQ(crash_analyzer->OnKernelPanicCrashLog(std::move(crash_log), &out_result), ZX_OK);
  EXPECT_TRUE(out_result.is_response());
}

// Smoke-tests the actual service for fuchsia.feedback.CrashReporter, connecting through FIDL.
TEST(CrashpadAgentIntegrationTest, CrashReporter_SmokeTest) {
  fuchsia::feedback::CrashReporterSyncPtr crash_reporter;
  auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(crash_reporter.NewRequest());

  // We use a generic crash report to smoke test the service is up and running because it has the
  // least expectations.
  fuchsia::feedback::GenericCrashReport generic_report;
  generic_report.set_program_name("crashing_program_generic");
  fuchsia::feedback::CrashReport report;
  report.set_generic(std::move(generic_report));

  fuchsia::feedback::CrashReporter_File_Result out_result;
  ASSERT_EQ(crash_reporter->File(std::move(report), &out_result), ZX_OK);
  EXPECT_TRUE(out_result.is_response());
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia
