// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/report_builder.h"

#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"

namespace forensics {
namespace exceptions {
namespace handler {
namespace {

const std::string kProcessName = "process_name";

class CrashReportBuilderTest : public testing::Test {
 protected:
  void SetUp() { builder_.SetProcessName(kProcessName); }

  CrashReportBuilder builder_;
};

TEST_F(CrashReportBuilderTest, SetsMinidump) {
  fsl::SizedVmo minidump_vmo;
  ASSERT_TRUE(fsl::VmoFromString("minidump", &minidump_vmo));

  builder_.SetMinidump(std::move(minidump_vmo.vmo()));

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_specific_report());
  ASSERT_TRUE(crash_report.specific_report().is_native());

  std::string minidump_content;
  ASSERT_TRUE(
      fsl::StringFromVmo(crash_report.specific_report().native().minidump(), &minidump_content));
  EXPECT_STREQ(minidump_content.c_str(), "minidump");
}

TEST_F(CrashReportBuilderTest, ProcessTerminated) {
  builder_.SetProcessTerminated();

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_specific_report());
  ASSERT_TRUE(crash_report.specific_report().is_generic());
  ASSERT_TRUE(crash_report.specific_report().generic().has_crash_signature());

  EXPECT_EQ(crash_report.specific_report().generic().crash_signature(),
            "fuchsia-no-minidump-process-terminated");
}

TEST_F(CrashReportBuilderTest, ExpiredException) {
  builder_.SetExceptionExpired();

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_specific_report());
  ASSERT_TRUE(crash_report.specific_report().is_generic());
  ASSERT_TRUE(crash_report.specific_report().generic().has_crash_signature());

  EXPECT_EQ(crash_report.specific_report().generic().crash_signature(),
            "fuchsia-no-minidump-exception-expired");
}

}  // namespace
}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
