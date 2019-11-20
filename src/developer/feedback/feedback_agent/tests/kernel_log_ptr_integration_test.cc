// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/boot/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include "src/developer/feedback/feedback_agent/attachments/kernel_log_ptr.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

class CollectKernelLogTest : public sys::testing::TestWithEnvironment {
 public:
  CollectKernelLogTest() : executor_(dispatcher()) {}

  void SetUp() override { environment_services_ = sys::ServiceDirectory::CreateFromNamespace(); }

  fit::result<fuchsia::mem::Buffer> GetKernelLog() {
    fit::result<fuchsia::mem::Buffer> result;
    const zx::duration timeout(zx::sec(10));
    bool done = false;
    executor_.schedule_task(CollectKernelLog(dispatcher(), environment_services_, timeout)
                                .then([&result, &done](fit::result<fuchsia::mem::Buffer>& res) {
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

  fit::result<fuchsia::mem::Buffer> result = GetKernelLog();
  ASSERT_TRUE(result.is_ok());
  fuchsia::mem::Buffer logs = result.take_value();
  std::string logs_as_string;
  ASSERT_TRUE(fsl::StringFromVmo(logs, &logs_as_string));
  EXPECT_THAT(logs_as_string, testing::HasSubstr(output));
}

TEST_F(CollectKernelLogTest, Succeed_TwoRetrievals) {
  // ReadOnlyLog was returning a shared handle so the second reader would get data after where the
  // first had read from. Confirm that both readers get the target string.
  const std::string output(
      fxl::StringPrintf("<<GetLogTest_Succeed_TwoRetrievals: %zu>>", zx_clock_get_monotonic()));
  SendToKernelLog(output);

  fit::result<fuchsia::mem::Buffer> result = GetKernelLog();
  ASSERT_TRUE(result.is_ok());
  fuchsia::mem::Buffer logs = result.take_value();
  std::string logs_as_string;
  ASSERT_TRUE(fsl::StringFromVmo(logs, &logs_as_string));
  EXPECT_THAT(logs_as_string, testing::HasSubstr(output));

  fit::result<fuchsia::mem::Buffer> second_result = GetKernelLog();
  ASSERT_TRUE(second_result.is_ok());
  fuchsia::mem::Buffer second_logs = second_result.take_value();
  std::string second_logs_as_string;
  ASSERT_TRUE(fsl::StringFromVmo(second_logs, &second_logs_as_string));
  EXPECT_THAT(second_logs_as_string, testing::HasSubstr(output));
}

TEST_F(CollectKernelLogTest, Fail_CallGetLogTwice) {
  const zx::duration unused_timeout = zx::sec(1);
  BootLog bootlog(dispatcher(), environment_services_);
  executor_.schedule_task(bootlog.GetLog(unused_timeout));
  ASSERT_DEATH(bootlog.GetLog(unused_timeout),
               testing::HasSubstr("GetLog() is not intended to be called twice"));
}

}  // namespace
}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
