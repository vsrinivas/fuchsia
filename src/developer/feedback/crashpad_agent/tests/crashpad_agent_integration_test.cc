// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/cobalt/test/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include "src/developer/feedback/testing/fake_cobalt.h"
#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/lib/fsl/vmo/strings.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using testing::UnorderedElementsAreArray;

class CrashpadAgentIntegrationTest : public testing::Test {
 public:
  void SetUp() override {
    environment_services_ = sys::ServiceDirectory::CreateFromNamespace();
    environment_services_->Connect(crash_reporter_.NewRequest());
    fake_cobalt_ = std::make_unique<FakeCobalt>(environment_services_);
  }

 protected:
  // Returns true if a response is received.
  void FileCrashReport() {
    fuchsia::feedback::CrashReport report;
    report.set_program_name("crashing_program");

    fuchsia::feedback::CrashReporter_File_Result out_result;
    ASSERT_EQ(crash_reporter_->File(std::move(report), &out_result), ZX_OK);
    EXPECT_TRUE(out_result.is_response());
  }

 private:
  std::shared_ptr<sys::ServiceDirectory> environment_services_;
  fuchsia::feedback::CrashReporterSyncPtr crash_reporter_;

 protected:
  std::unique_ptr<FakeCobalt> fake_cobalt_;
};

// Smoke-tests the actual service for fuchsia.feedback.CrashReporter, connecting through FIDL.
TEST_F(CrashpadAgentIntegrationTest, CrashReporter_SmokeTest) {
  FileCrashReport();

  EXPECT_THAT(fake_cobalt_->GetAllEventsOfType<CrashState>(
                  /*num_expected=*/2, fuchsia::cobalt::test::LogMethod::LOG_EVENT),
              UnorderedElementsAreArray({
                  CrashState::kFiled,
                  CrashState::kArchived,
              }));
}

}  // namespace
}  // namespace feedback
