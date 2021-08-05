// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/boot/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/attachments/kernel_log_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace {

using testing::UnorderedElementsAreArray;

class CollectKernelLogTest : public gtest::TestWithEnvironmentFixture {
 public:
  CollectKernelLogTest() : executor_(dispatcher()) {}

  void SetUp() override { environment_services_ = sys::ServiceDirectory::CreateFromNamespace(); }

  ::fpromise::result<AttachmentValue> GetKernelLog(const zx::duration timeout = zx::sec(10)) {
    ::fpromise::result<AttachmentValue> result;
    bool done = false;
    executor_.schedule_task(
        CollectKernelLog(dispatcher(), environment_services_, fit::Timeout(timeout))
            .then([&result, &done](::fpromise::result<AttachmentValue>& res) {
              result = std::move(res);
              done = true;
            }));
    RunLoopUntil([&done] { return done; });
    return result;
  }

 protected:
  std::shared_ptr<sys::ServiceDirectory> environment_services_;
  async::Executor executor_;
};

void SendToKernelLog(const std::string& str) {
  zx::channel local, remote;
  ASSERT_EQ(zx::channel::create(0, &local, &remote), ZX_OK);
  constexpr char kWriteOnlyLogPath[] = "/svc/" fuchsia_boot_WriteOnlyLog_Name;
  ASSERT_EQ(fdio_service_connect(kWriteOnlyLogPath, remote.release()), ZX_OK);

  zx::debuglog log;
  ASSERT_EQ(fuchsia_boot_WriteOnlyLogGet(local.get(), log.reset_and_get_address()), ZX_OK);

  zx_debuglog_write(log.get(), 0, str.c_str(), str.size());
}

TEST_F(CollectKernelLogTest, Succeed_BasicCase) {
  const std::string output(
      fxl::StringPrintf("<<GetLogTest_Succeed_BasicCase: %zu>>", zx_clock_get_monotonic()));
  SendToKernelLog(output);

  ::fpromise::result<AttachmentValue> result = GetKernelLog();
  ASSERT_TRUE(result.is_ok());
  AttachmentValue logs = result.take_value();

  ASSERT_TRUE(logs.HasValue());
  EXPECT_THAT(logs.Value(), testing::HasSubstr(output));
}

TEST_F(CollectKernelLogTest, Succeed_TwoRetrievals) {
  // ReadOnlyLog was returning a shared handle so the second reader would get data after where the
  // first had read from. Confirm that both readers get the target string.
  const std::string output(
      fxl::StringPrintf("<<GetLogTest_Succeed_TwoRetrievals: %zu>>", zx_clock_get_monotonic()));
  SendToKernelLog(output);

  ::fpromise::result<AttachmentValue> result = GetKernelLog();
  ASSERT_TRUE(result.is_ok());
  AttachmentValue logs = result.take_value();

  ASSERT_TRUE(logs.HasValue());
  EXPECT_THAT(logs.Value(), testing::HasSubstr(output));

  ::fpromise::result<AttachmentValue> second_result = GetKernelLog();
  ASSERT_TRUE(second_result.is_ok());
  AttachmentValue second_logs = second_result.take_value();

  ASSERT_TRUE(second_logs.HasValue());
  EXPECT_THAT(second_logs.Value(), testing::HasSubstr(output));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
