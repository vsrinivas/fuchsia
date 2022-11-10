// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/boot/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/attachments/kernel_log.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace forensics::feedback {
namespace {

using testing::UnorderedElementsAreArray;

class CollectKernelLogTest : public gtest::RealLoopFixture {
 public:
  CollectKernelLogTest() : executor_(dispatcher()) {
    environment_services_ = sys::ServiceDirectory::CreateFromNamespace();
    redactor_ = std::unique_ptr<RedactorBase>(new IdentityRedactor(inspect::BoolProperty()));
  }

  void SetRedactor(std::unique_ptr<RedactorBase> redactor) { redactor_ = std::move(redactor); }

  AttachmentValue GetKernelLog() {
    const uint64_t kTicket = 1234;
    KernelLog kernel_log(dispatcher(), environment_services_, nullptr, redactor_.get());
    ::fpromise::result<AttachmentValue> attachment(::fpromise::error());
    executor_.schedule_task(kernel_log.Get(kTicket)
                                .and_then([&attachment](AttachmentValue& result) {
                                  attachment = ::fpromise::ok(std::move(result));
                                })
                                .or_else([] { FX_LOGS(FATAL) << "Unreachable branch"; }));

    RunLoopUntil([&attachment] { return attachment.is_ok(); });
    return attachment.take_value();
  }

 protected:
  async::Executor& GetExecutor() { return executor_; }

 protected:
  std::shared_ptr<sys::ServiceDirectory> environment_services_;
  async::Executor executor_;
  std::unique_ptr<RedactorBase> redactor_;
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

  const auto log = GetKernelLog();

  ASSERT_TRUE(log.HasValue());
  EXPECT_THAT(log.Value(), testing::HasSubstr(output));
}

TEST_F(CollectKernelLogTest, GetTerminatesDueToForceCompletion) {
  const std::string output(fxl::StringPrintf(
      "<<GetLogTest_Get_Terminates_Due_To_ForceCompletion: %zu>>", zx_clock_get_monotonic()));
  const uint64_t kTicket = 1234;

  SendToKernelLog(output);

  AttachmentValue log(Error::kNotSet);
  ::fpromise::result<AttachmentValue> attachment(::fpromise::error());

  KernelLog kernel_log(dispatcher(), environment_services_, nullptr, redactor_.get());
  GetExecutor().schedule_task(kernel_log.Get(kTicket).and_then(
      [&attachment](AttachmentValue& result) { attachment = ::fpromise::ok(std::move(result)); }));
  kernel_log.ForceCompletion(kTicket, Error::kDefault);

  RunLoopUntil([&attachment] { return attachment.is_ok(); });
  log = attachment.take_value();

  EXPECT_FALSE(log.HasValue());
  ASSERT_TRUE(log.HasError());
  EXPECT_EQ(log.Error(), Error::kDefault);
}

TEST_F(CollectKernelLogTest, ForceCompletionCalledAfterTermination) {
  const std::string output(fxl::StringPrintf(
      "<<GetLogTest_ForceCompletion_Called_After_Termination: %zu>>", zx_clock_get_monotonic()));
  const uint64_t kTicket = 1234;

  SendToKernelLog(output);

  AttachmentValue log(Error::kNotSet);
  ::fpromise::result<AttachmentValue> attachment(::fpromise::error());

  KernelLog kernel_log(dispatcher(), environment_services_, nullptr, redactor_.get());
  GetExecutor().schedule_task(kernel_log.Get(kTicket).and_then(
      [&attachment](AttachmentValue& result) { attachment = ::fpromise::ok(std::move(result)); }));

  RunLoopUntil([&attachment] { return attachment.is_ok(); });
  log = attachment.take_value();

  kernel_log.ForceCompletion(kTicket, Error::kDefault);

  ASSERT_FALSE(log.HasError());

  ASSERT_TRUE(log.HasValue());
  EXPECT_THAT(log.Value(), testing::HasSubstr(output));
}

TEST_F(CollectKernelLogTest, GetCalledWithSameTicket) {
  const uint64_t kTicket = 1234;

  KernelLog kernel_log(dispatcher(), environment_services_, nullptr, redactor_.get());

  // Expect a crash because a ticket cannot be reused.
  ASSERT_DEATH(
      {
        const auto log1 = kernel_log.Get(kTicket);
        const auto log2 = kernel_log.Get(kTicket);
      },
      "Ticket used twice: ");
}

TEST_F(CollectKernelLogTest, Succeed_TwoRetrievals) {
  // ReadOnlyLog was returning a shared handle so the second reader would get data after where the
  // first had read from. Confirm that both readers get the target string.
  const std::string output(
      fxl::StringPrintf("<<GetLogTest_Succeed_TwoRetrievals: %zu>>", zx_clock_get_monotonic()));
  SendToKernelLog(output);

  const auto log1 = GetKernelLog();

  ASSERT_TRUE(log1.HasValue());
  EXPECT_THAT(log1.Value(), testing::HasSubstr(output));

  const auto log2 = GetKernelLog();

  ASSERT_TRUE(log2.HasValue());
  EXPECT_THAT(log2.Value(), testing::HasSubstr(output));
}

class SimpleRedactor : public RedactorBase {
 public:
  SimpleRedactor() : RedactorBase(inspect::BoolProperty()) {}

  std::string& Redact(std::string& text) override {
    text = "<REDACTED>";
    return text;
  }

  std::string UnredactedCanary() const override { return ""; }
  std::string RedactedCanary() const override { return ""; }
};

TEST_F(CollectKernelLogTest, Succeed_Redacts) {
  SetRedactor(std::make_unique<SimpleRedactor>());
  const std::string output(
      fxl::StringPrintf("<<GetLogTest_Succeed_BasicCase: %zu>>", zx_clock_get_monotonic()));
  SendToKernelLog(output);

  const auto log = GetKernelLog();

  ASSERT_TRUE(log.HasValue());
  EXPECT_THAT(log.Value(), testing::HasSubstr("<REDACTED>"));
}

}  // namespace
}  // namespace forensics::feedback
