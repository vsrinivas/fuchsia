// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/bugreport/bug_reporter.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/errors.h>

#include <memory>
#include <string>

#include "src/developer/feedback/bugreport/tests/stub_feedback_data_provider.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

class BugReporterTest : public gtest::TestLoopFixture {
 public:
  BugReporterTest()
      : service_directory_provider_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        service_directory_provider_(service_directory_provider_loop_.dispatcher()) {}

  void SetUp() override {
    // We run the service directory provider in a different loop and thread so that the
    // MakeBugReport can connect to the stub feedback data provider synchronously.
    ASSERT_EQ(service_directory_provider_loop_.StartThread("service directory provider thread"),
              ZX_OK);

    ASSERT_TRUE(tmp_dir_.NewTempFile(&bugreport_path_));
  }

 protected:
  void SetUpFeedbackDataProvider(fuchsia::feedback::Attachment attachment_bundle) {
    feedback_data_provider_ =
        std::make_unique<StubFeedbackDataProvider>(std::move(attachment_bundle));
    ASSERT_EQ(service_directory_provider_.AddService(feedback_data_provider_->GetHandler()), ZX_OK);
  }

 private:
  async::Loop service_directory_provider_loop_;

 protected:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::string bugreport_path_;

 private:
  std::unique_ptr<StubFeedbackDataProvider> feedback_data_provider_;
  files::ScopedTempDir tmp_dir_;
};

TEST_F(BugReporterTest, Basic) {
  const std::string payload = "technically a ZIP archive, but it doesn't matter for the unit test";

  fuchsia::feedback::Attachment attachment_bundle;
  attachment_bundle.key = "unused";
  ASSERT_TRUE(fsl::VmoFromString(payload, &attachment_bundle.value));
  SetUpFeedbackDataProvider(std::move(attachment_bundle));

  ASSERT_TRUE(
      MakeBugReport(service_directory_provider_.service_directory(), bugreport_path_.data()));

  std::string bugreport;
  ASSERT_TRUE(files::ReadFileToString(bugreport_path_, &bugreport));
  EXPECT_STREQ(bugreport.c_str(), payload.c_str());
}

}  // namespace
}  // namespace feedback
