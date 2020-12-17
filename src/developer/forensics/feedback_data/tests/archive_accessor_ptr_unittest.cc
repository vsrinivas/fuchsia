// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"

namespace forensics {
namespace feedback_data {
namespace {

// Collects Archive data in snapshot mode; concatenates data in the output string "contents" and
// returns a promise of the done signal. After every Collect(), "." is also appended.
::fit::promise<void, Error> CollectArchiveData(async_dispatcher_t* dispatcher,
                                               std::shared_ptr<sys::ServiceDirectory> services,
                                               fit::Timeout timeout,
                                               std::shared_ptr<std::string> content,
                                               std::optional<size_t> data_budget) {
  std::unique_ptr<ArchiveAccessor> inspect = std::make_unique<ArchiveAccessor>(
      dispatcher, services, fuchsia::diagnostics::DataType::INSPECT,
      fuchsia::diagnostics::StreamMode::SNAPSHOT, data_budget);

  // Collect data.
  inspect->Collect([content](fuchsia::diagnostics::FormattedContent chunk) {
    std::string json;
    fsl::StringFromVmo(chunk.json(), &json);
    *content.get() += json + ".";
  });

  // Wait for done signal.
  ::fit::promise<void, Error> inspect_data = inspect->WaitForDone(std::move(timeout));

  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(inspect_data),
                                              /*args=*/std::move(inspect));
}

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

class ArchiveAccessorTest : public UnitTestFixture {
 public:
  ArchiveAccessorTest() : executor_(dispatcher()) {}

 protected:
  void SetUpInspectServer(std::unique_ptr<stubs::DiagnosticsArchiveBase> server) {
    inspect_server_ = std::move(server);
    if (inspect_server_) {
      InjectServiceProvider(inspect_server_.get(), kArchiveAccessorName);
    }
  }

  ::fit::result<void, Error> CollectData(std::shared_ptr<std::string> content,
                                         std::optional<size_t> data_budget = {}) {
    ::fit::result<void, Error> status;

    const zx::duration timeout = zx::sec(1);
    executor_.schedule_task(
        feedback_data::CollectArchiveData(
            dispatcher(), services(), fit::Timeout(timeout, /*action=*/[] {}), content, data_budget)
            .then([&status](::fit::result<void, Error>& res) { status = std::move(res); }));
    RunLoopFor(timeout);
    return status;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<stubs::DiagnosticsArchiveBase> inspect_server_;
};

TEST_F(ArchiveAccessorTest, TestLimitedDataBudget) {
  fuchsia::diagnostics::StreamParameters parameters;
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchiveCaptureParameters>(&parameters));

  auto budget = std::make_optional<size_t>(1024);
  CollectData(std::make_shared<std::string>(), budget);

  // read parameters
  ASSERT_TRUE(parameters.has_performance_configuration());
  const auto& performance = parameters.performance_configuration();
  ASSERT_TRUE(performance.has_max_aggregate_content_size_bytes());
  ASSERT_EQ(performance.max_aggregate_content_size_bytes(), budget.value());
}

TEST_F(ArchiveAccessorTest, TestUnlimitedDataBudget) {
  fuchsia::diagnostics::StreamParameters parameters;
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchiveCaptureParameters>(&parameters));

  CollectData(std::make_shared<std::string>(), {});

  // read parameters
  ASSERT_FALSE(parameters.has_performance_configuration());
}

TEST_F(ArchiveAccessorTest, Succeed_AllInspectData) {
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {"foo1", "foo2"},
          {"bar1"},
          {},
      }))));

  auto content = std::make_shared<std::string>();
  auto status = CollectData(content);
  ASSERT_TRUE(status.is_ok());

  ASSERT_STREQ(content->c_str(), "foo1.foo2.bar1.");
}

TEST_F(ArchiveAccessorTest, Succeed_PartialInspectData) {
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverRespondsAfterOneBatch>(
          std::vector<std::string>({"foo1", "foo2"}))));

  auto content = std::make_shared<std::string>();
  auto status = CollectData(content);
  ASSERT_FALSE(status.is_ok());

  EXPECT_STREQ(content->c_str(), "foo1.foo2.");
  EXPECT_EQ(status.error(), Error::kTimeout);
}

TEST_F(ArchiveAccessorTest, Fail_BatchIteratorReturnsError) {
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorReturnsError>()));

  auto content = std::make_shared<std::string>();
  auto status = CollectData(content);
  ASSERT_FALSE(status.is_ok());
  EXPECT_EQ(status.error(), Error::kBadValue);
}

TEST_F(ArchiveAccessorTest, Fail_BatchIteratorNeverResponds) {
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverResponds>()));

  auto content = std::make_shared<std::string>();
  auto status = CollectData(content);
  ASSERT_FALSE(status.is_ok());
  EXPECT_EQ(status.error(), Error::kTimeout);
}

TEST_F(ArchiveAccessorTest, Fail_ArchiveClosesIteratorClosesConnection) {
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchiveClosesIteratorConnection>());

  auto content = std::make_shared<std::string>();
  auto status = CollectData(content);
  ASSERT_FALSE(status.is_ok());
  EXPECT_EQ(status.error(), Error::kConnectionError);
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
