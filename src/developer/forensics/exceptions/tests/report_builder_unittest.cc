// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/report_builder.h"

#include <gtest/gtest.h>

#include "src/developer/forensics/exceptions/tests/crasher_wrapper.h"
#include "src/lib/fsl/vmo/strings.h"

namespace forensics {
namespace exceptions {
namespace handler {
namespace {

class CrashReportBuilderTest : public testing::Test {
 protected:
  // Use an invalid process/thread becuase we don't care about the specific name and koid of each.
  void SetUp() { builder_.SetProcess(zx::process{}).SetThread(zx::thread{}); }

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

TEST_F(CrashReportBuilderTest, ExceptionReason_ChannelOverflow) {
  fsl::SizedVmo minidump_vmo;
  ASSERT_TRUE(fsl::VmoFromString("minidump", &minidump_vmo));

  builder_.SetMinidump(std::move(minidump_vmo.vmo()))
      .SetExceptionReason(ExceptionReason::kChannelOverflow);

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_crash_signature());
  EXPECT_EQ(crash_report.crash_signature(), "fuchsia-unknown_process-channel-overflow");
}

TEST_F(CrashReportBuilderTest, ExceptionReason_PortOverflow) {
  fsl::SizedVmo minidump_vmo;
  ASSERT_TRUE(fsl::VmoFromString("minidump", &minidump_vmo));

  builder_.SetMinidump(std::move(minidump_vmo.vmo()))
      .SetExceptionReason(ExceptionReason::kPortOverflow);

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_crash_signature());
  EXPECT_EQ(crash_report.crash_signature(), "fuchsia-unknown_process-port-overflow");
}

TEST_F(CrashReportBuilderTest, ExceptionReason_PageFaultIo) {
  fsl::SizedVmo minidump_vmo;
  ASSERT_TRUE(fsl::VmoFromString("minidump", &minidump_vmo));

  builder_.SetMinidump(std::move(minidump_vmo.vmo()))
      .SetExceptionReason(ExceptionReason::kPageFaultIo);

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_crash_signature());
  EXPECT_EQ(crash_report.crash_signature(), "fuchsia-page_fault-io");
}

TEST_F(CrashReportBuilderTest, ExceptionReason_PageFaultIoDataIntegrity) {
  fsl::SizedVmo minidump_vmo;
  ASSERT_TRUE(fsl::VmoFromString("minidump", &minidump_vmo));

  builder_.SetMinidump(std::move(minidump_vmo.vmo()))
      .SetExceptionReason(ExceptionReason::kPageFaultIoDataIntegrity);

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_crash_signature());
  EXPECT_EQ(crash_report.crash_signature(), "fuchsia-page_fault-io_data_integrity");
}

TEST_F(CrashReportBuilderTest, ExceptionReason_PageFaultBadState) {
  fsl::SizedVmo minidump_vmo;
  ASSERT_TRUE(fsl::VmoFromString("minidump", &minidump_vmo));

  builder_.SetMinidump(std::move(minidump_vmo.vmo()))
      .SetExceptionReason(ExceptionReason::kPageFaultBadState);

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_crash_signature());
  EXPECT_EQ(crash_report.crash_signature(), "fuchsia-page_fault-bad_state");
}

TEST_F(CrashReportBuilderTest, ProcessTerminated) {
  builder_.SetProcessTerminated();

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_specific_report());

  ASSERT_TRUE(crash_report.has_program_name());
  ASSERT_EQ(crash_report.program_name(), "unknown_process");

  ASSERT_FALSE(crash_report.has_program_uptime());

  ASSERT_TRUE(crash_report.specific_report().is_native());
  EXPECT_FALSE(crash_report.specific_report().native().has_minidump());

  ASSERT_TRUE(crash_report.has_crash_signature());
  EXPECT_EQ(crash_report.crash_signature(), "fuchsia-no-minidump-process-terminated");
}

TEST_F(CrashReportBuilderTest, ExpiredException) {
  builder_.SetExceptionExpired();

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_specific_report());

  ASSERT_TRUE(crash_report.specific_report().is_native());
  EXPECT_FALSE(crash_report.specific_report().native().has_minidump());

  ASSERT_TRUE(crash_report.has_crash_signature());
  EXPECT_EQ(crash_report.crash_signature(), "fuchsia-no-minidump-exception-expired");
}

TEST_F(CrashReportBuilderTest, IsFatal) {
  builder_.SetExceptionExpired();

  auto crash_report = builder_.Consume();
  ASSERT_TRUE(crash_report.has_is_fatal());
  EXPECT_TRUE(crash_report.is_fatal());
}

TEST(ReportBuilderTest, TestUptime) {
  CrashReportBuilder builder;
  ExceptionContext exception;

  // Spawn the 'crasher' process.
  ASSERT_TRUE(SpawnCrasher(&exception));
  // If we don't mark it as handled, the exception will bubble out of our environment.
  ASSERT_TRUE(MarkExceptionAsHandled(&exception));

  zx::process process;
  zx::thread thread;
  ASSERT_EQ(exception.exception.get_process(&process), ZX_OK);
  ASSERT_EQ(exception.exception.get_thread(&thread), ZX_OK);
  builder.SetProcess(process).SetThread(thread);
  builder.SetProcessTerminated();

  auto crash_report = builder.Consume();
  ASSERT_TRUE(crash_report.has_program_name());
  ASSERT_EQ(crash_report.program_name(), "crasher");
  ASSERT_TRUE(crash_report.has_program_uptime());
  ASSERT_GE(crash_report.program_uptime(), 0);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

}  // namespace
}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
