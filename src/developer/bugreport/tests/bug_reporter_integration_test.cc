// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/service_directory.h>

#include "src/developer/bugreport/bug_report_schema.h"
#include "src/developer/bugreport/bug_reporter.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"

namespace fuchsia {
namespace bugreport {
namespace {

class BugReporterIntegrationTest : public testing::Test {
 public:
  void SetUp() override {
    environment_services_ = ::sys::ServiceDirectory::CreateFromNamespace();
    ASSERT_TRUE(tmp_dir_.NewTempFile(&json_path_));
  }

 protected:
  std::shared_ptr<::sys::ServiceDirectory> environment_services_;
  std::string json_path_;

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(BugReporterIntegrationTest, SmokeTest) {
  ASSERT_TRUE(MakeBugReport(environment_services_, json_path_.data()));

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
}

}  // namespace
}  // namespace bugreport
}  // namespace fuchsia
