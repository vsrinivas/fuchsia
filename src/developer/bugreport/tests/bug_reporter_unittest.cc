// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/bugreport/bug_reporter.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/errors.h>

#include <map>
#include <memory>
#include <string>

#include "src/developer/bugreport/bug_report_schema.h"
#include "src/developer/bugreport/tests/stub_feedback_data_provider.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"

namespace fuchsia {
namespace bugreport {
namespace {

class BugReporterTest : public gtest::RealLoopFixture {
 public:
  BugReporterTest()
      : service_directory_provider_loop_(&kAsyncLoopConfigNoAttachToThread),
        service_directory_provider_(
            service_directory_provider_loop_.dispatcher()) {
    // We run the service directory provider in a different loop and thread so
    // that the MakeBugReport can connect to the stub feedback data provider
    // synchronously.
    FXL_CHECK(service_directory_provider_loop_.StartThread(
                  "service directory provider thread") == ZX_OK);
  }

  void SetUp() override { ASSERT_TRUE(tmp_dir_.NewTempFile(&json_path_)); }

 protected:
  void ResetFeedbackDataProvider(
      const std::map<std::string, std::string>& annotations,
      const std::map<std::string, std::string>& attachments) {
    stub_feedback_data_provider_.reset(
        new StubFeedbackDataProvider(annotations, attachments));
    FXL_CHECK(service_directory_provider_.AddService(
                  stub_feedback_data_provider_->GetHandler()) == ZX_OK);
  }

 private:
  async::Loop service_directory_provider_loop_;

 protected:
  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::string json_path_;

 private:
  std::unique_ptr<StubFeedbackDataProvider> stub_feedback_data_provider_;
  files::ScopedTempDir tmp_dir_;
};

TEST_F(BugReporterTest, SmokeTest) {
  ResetFeedbackDataProvider(/*annotations=*/
                            {
                                {"annotation.1.key", "annotation.1.value"},
                                {"annotation.2.key", "annotation.2.value"},
                                {"annotation.3.key", "annotation.3.value"},
                            },
                            /*attachments=*/{
                                {"attachment.1.key", "attachment.1.value"},
                                {"attachment.2.key", "attachment.2.value"},
                            });

  ASSERT_TRUE(MakeBugReport(service_directory_provider_.service_directory(),
                            json_path_.data()));

  std::string output;
  ASSERT_TRUE(files::ReadFileToString(json_path_, &output));

  // JSON verification.
  // We check that the output is a valid JSON and that it matches the schema.
  rapidjson::Document document;
  ASSERT_FALSE(document.Parse(output.c_str()).HasParseError());
  rapidjson::Document document_schema;
  ASSERT_FALSE(document_schema.Parse(kBugReportJsonSchema).HasParseError());
  rapidjson::SchemaDocument schema(document_schema);
  rapidjson::SchemaValidator validator(schema);
  EXPECT_TRUE(document.Accept(validator));

  // Content verification.
  ASSERT_TRUE(document.HasMember("annotations"));
  ASSERT_TRUE(document["annotations"].HasMember("annotation.1.key"));
  ASSERT_TRUE(document["annotations"].HasMember("annotation.2.key"));
  ASSERT_TRUE(document["annotations"].HasMember("annotation.3.key"));
  ASSERT_TRUE(document["annotations"]["annotation.1.key"].IsString());
  ASSERT_TRUE(document["annotations"]["annotation.2.key"].IsString());
  ASSERT_TRUE(document["annotations"]["annotation.3.key"].IsString());
  EXPECT_STREQ(document["annotations"]["annotation.1.key"].GetString(),
               "annotation.1.value");
  EXPECT_STREQ(document["annotations"]["annotation.2.key"].GetString(),
               "annotation.2.value");
  EXPECT_STREQ(document["annotations"]["annotation.3.key"].GetString(),
               "annotation.3.value");

  ASSERT_TRUE(document.HasMember("attachments"));
  ASSERT_TRUE(document["attachments"].HasMember("attachment.1.key"));
  ASSERT_TRUE(document["attachments"].HasMember("attachment.2.key"));
  ASSERT_TRUE(document["attachments"]["attachment.1.key"].IsString());
  ASSERT_TRUE(document["attachments"]["attachment.2.key"].IsString());
  EXPECT_STREQ(document["attachments"]["attachment.1.key"].GetString(),
               "attachment.1.value");
  EXPECT_STREQ(document["attachments"]["attachment.2.key"].GetString(),
               "attachment.2.value");
}

}  // namespace
}  // namespace bugreport
}  // namespace fuchsia
