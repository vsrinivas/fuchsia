// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/errors.h>

#include "src/lib/fsl/vmo/strings.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

// Smoke-tests the actual service for fuchsia.feedback.CrashReporter, connecting through FIDL.
TEST(CrashpadAgentIntegrationTest, CrashReporter_SmokeTest) {
  fuchsia::feedback::CrashReporterSyncPtr crash_reporter;
  auto environment_services = sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(crash_reporter.NewRequest());

  fuchsia::feedback::CrashReport report;
  report.set_program_name("crashing_program");

  fuchsia::feedback::CrashReporter_File_Result out_result;
  ASSERT_EQ(crash_reporter->File(std::move(report), &out_result), ZX_OK);
  EXPECT_TRUE(out_result.is_response());
}

}  // namespace
}  // namespace feedback
