// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/inspect_ptr.h"

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

#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace feedback_data {
namespace {

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

class CollectInspectDataTest : public UnitTestFixture {
 public:
  CollectInspectDataTest() : executor_(dispatcher()) {}

 protected:
  void SetUpInspectServer(std::unique_ptr<stubs::DiagnosticsArchiveBase> server) {
    inspect_server_ = std::move(server);
    if (inspect_server_) {
      InjectServiceProvider(inspect_server_.get(), kArchiveAccessorName);
    }
  }

  ::fit::result<AttachmentValue> CollectInspectData(const zx::duration timeout = zx::sec(1)) {
    ::fit::result<AttachmentValue> result;
    executor_.schedule_task(
        feedback_data::CollectInspectData(dispatcher(), services(),
                                          fit::Timeout(timeout, /*action=*/[] {}), {})
            .then([&result](::fit::result<AttachmentValue>& res) { result = std::move(res); }));
    RunLoopFor(timeout);
    return result;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<stubs::DiagnosticsArchiveBase> inspect_server_;
};

TEST_F(CollectInspectDataTest, Succeed_AllInspectData) {
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {"foo1", "foo2"},
          {"bar1"},
          {},
      }))));

  ::fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& inspect = result.value();
  ASSERT_EQ(inspect.State(), AttachmentValue::State::kComplete);
  ASSERT_STREQ(inspect.Value().c_str(), R"([
foo1,
foo2,
bar1
])");
}

TEST_F(CollectInspectDataTest, Succeed_PartialInspectData) {
  SetUpInspectServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverRespondsAfterOneBatch>(
          std::vector<std::string>({"foo1", "foo2"}))));

  ::fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& inspect = result.value();
  ASSERT_EQ(inspect.State(), AttachmentValue::State::kPartial);
  EXPECT_STREQ(inspect.Value().c_str(), R"([
foo1,
foo2
])");
  EXPECT_EQ(inspect.Error(), Error::kTimeout);
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
