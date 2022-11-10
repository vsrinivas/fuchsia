// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/attachments/inspect.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/timekeeper/async_test_clock.h"

namespace forensics::feedback {
namespace {

class MonotonicBackoff : public backoff::Backoff {
 public:
  static std::unique_ptr<backoff::Backoff> Make() { return std::make_unique<MonotonicBackoff>(); }

  zx::duration GetNext() override { return zx::sec(delay_++); }
  void Reset() override {}

 private:
  size_t delay_{1u};
};

class InspectTest : public UnitTestFixture {
 public:
  InspectTest()
      : executor_(dispatcher()),
        clock_(dispatcher()),
        cobalt_(dispatcher(), services(), &clock_),
        inspect_node_manager_(&InspectRoot()),
        inspect_data_budget_(std::make_unique<feedback_data::InspectDataBudget>(
            true, &inspect_node_manager_, &cobalt_)) {}

 protected:
  void SetUpInspectServer(std::unique_ptr<stubs::DiagnosticsArchiveBase> server) {
    inspect_server_ = std::move(server);
    if (inspect_server_) {
      InjectServiceProvider(inspect_server_.get(), feedback_data::kArchiveAccessorName);
    }
  }

  void DisableDataBudget() {
    inspect_data_budget_ =
        std::make_unique<feedback_data::InspectDataBudget>(false, &inspect_node_manager_, &cobalt_);
  }

  AttachmentValue Run(::fpromise::promise<AttachmentValue> promise,
                      const std::optional<zx::duration> run_loop_for = std::nullopt) {
    AttachmentValue attachment(Error::kNotSet);
    executor_.schedule_task(
        promise.and_then([&attachment](AttachmentValue& result) { attachment = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unexpected branch"; }));

    if (run_loop_for.has_value()) {
      RunLoopFor(*run_loop_for);
    } else {
      RunLoopUntilIdle();
    }

    return attachment;
  }

  feedback_data::InspectDataBudget* DataBudget() { return inspect_data_budget_.get(); }

 private:
  async::Executor executor_;
  timekeeper::AsyncTestClock clock_;
  cobalt::Logger cobalt_;
  InspectNodeManager inspect_node_manager_;
  std::unique_ptr<feedback_data::InspectDataBudget> inspect_data_budget_;
  std::unique_ptr<stubs::DiagnosticsArchiveBase> inspect_server_;
};

TEST_F(InspectTest, DataBudget) {
  const uint64_t kTicket = 1234;
  fuchsia::diagnostics::StreamParameters parameters;
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchiveCaptureParameters>(&parameters));

  const size_t kBudget = DataBudget()->SizeInBytes().value();
  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());

  inspect.Get(kTicket);
  RunLoopUntilIdle();

  ASSERT_TRUE(parameters.has_performance_configuration());
  const auto& performance = parameters.performance_configuration();
  ASSERT_TRUE(performance.has_max_aggregate_content_size_bytes());
  ASSERT_EQ(performance.max_aggregate_content_size_bytes(), kBudget);
}

TEST_F(InspectTest, NoDataBudget) {
  const uint64_t kTicket = 1234;
  fuchsia::diagnostics::StreamParameters parameters;
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchiveCaptureParameters>(&parameters));

  DisableDataBudget();
  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());

  inspect.Get(kTicket);
  RunLoopUntilIdle();

  EXPECT_FALSE(parameters.has_performance_configuration());
}

TEST_F(InspectTest, Get) {
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {"foo1", "foo2"},
          {"bar1"},
          {},
      }))));

  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());
  const auto attachment = Run(inspect.Get(1234));

  EXPECT_FALSE(attachment.HasError());

  ASSERT_TRUE(attachment.HasValue());
  EXPECT_EQ(attachment.Value(), R"([
foo1,
foo2,
bar1
])");
}

TEST_F(InspectTest, GetTerminatesDueToForceCompletion) {
  const uint64_t kTicket = 1234;
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverRespondsAfterOneBatch>(
          std::vector<std::string>({"foo1", "foo2"}))));

  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());
  const auto attachment = Run(inspect.Get(kTicket));

  // Giving some time to actually collect some inspect data
  RunLoopUntilIdle();

  // Forcefully terminate inspect collection
  inspect.ForceCompletion(kTicket, Error::kDefault);

  RunLoopUntilIdle();

  ASSERT_TRUE(attachment.HasError());
  EXPECT_EQ(attachment.Error(), Error::kDefault);

  ASSERT_TRUE(attachment.HasValue());
  EXPECT_EQ(attachment.Value(), R"([
foo1,
foo2
])");
}

TEST_F(InspectTest, ForceCompletionCalledAfterTermination) {
  const uint64_t kTicket = 1234;
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {"foo1", "foo2"},
          {"bar1"},
          {},
      }))));

  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());
  const auto attachment = Run(inspect.Get(kTicket));

  inspect.ForceCompletion(kTicket, Error::kDefault);

  ASSERT_FALSE(attachment.HasError());

  ASSERT_TRUE(attachment.HasValue());
  EXPECT_EQ(attachment.Value(), R"([
foo1,
foo2,
bar1
])");
}

TEST_F(InspectTest, GetCalledWithSameTicket) {
  const uint64_t kTicket = 1234;
  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());

  // Expect a crash because a ticket cannot be reused.
  ASSERT_DEATH(
      {
        const auto attachment1 = inspect.Get(kTicket);
        const auto attachment2 = inspect.Get(kTicket);
      },
      "Ticket used twice: ");
}

TEST_F(InspectTest, GetConnectionError) {
  const uint64_t kTicket = 1234;
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchiveClosesIteratorConnection>());

  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());
  const auto attachment = Run(inspect.Get(kTicket));

  ASSERT_TRUE(attachment.HasError());
  EXPECT_EQ(attachment.Error(), Error::kConnectionError);

  EXPECT_FALSE(attachment.HasValue());
}

TEST_F(InspectTest, GetIteratorReturnsError) {
  const uint64_t kTicket = 1234;
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorReturnsError>()));

  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());
  const auto attachment = Run(inspect.Get(kTicket));

  ASSERT_TRUE(attachment.HasError());
  EXPECT_EQ(attachment.Error(), Error::kMissingValue);

  EXPECT_FALSE(attachment.HasValue());
}

TEST_F(InspectTest, Reconnects) {
  fuchsia::diagnostics::StreamParameters parameters;
  auto archive = std::make_unique<stubs::DiagnosticsArchiveCaptureParameters>(&parameters);

  InjectServiceProvider(archive.get(), feedback_data::kArchiveAccessorName);

  Inspect inspect(dispatcher(), services(), MonotonicBackoff::Make(), DataBudget());
  RunLoopUntilIdle();

  EXPECT_TRUE(archive->IsBound());

  archive->CloseConnection();
  RunLoopUntilIdle();

  EXPECT_FALSE(archive->IsBound());

  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(archive->IsBound());
}

}  // namespace
}  // namespace forensics::feedback
