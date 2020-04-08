// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/inspect_ptr.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/attachments/aliases.h"
#include "src/developer/feedback/testing/stubs/inspect_archive.h"
#include "src/developer/feedback/testing/stubs/inspect_batch_iterator.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

class CollectInspectDataTest : public UnitTestFixture {
 public:
  CollectInspectDataTest() : executor_(dispatcher()) {}

 protected:
  void SetUpInspectServer(std::unique_ptr<stubs::InspectArchiveBase> server) {
    inspect_server_ = std::move(server);
    if (inspect_server_) {
      InjectServiceProvider(inspect_server_.get());
    }
  }

  ::fit::result<AttachmentValue> CollectInspectData(const zx::duration timeout = zx::sec(1)) {
    ::fit::result<AttachmentValue> result;
    executor_.schedule_task(
        feedback::CollectInspectData(dispatcher(), services(),
                                     fit::Timeout(timeout, /*action=*/[&] { did_timeout_ = true; }))
            .then([&result](::fit::result<AttachmentValue>& res) { result = std::move(res); }));
    RunLoopFor(timeout);
    return result;
  }

  async::Executor executor_;
  bool did_timeout_ = false;

 private:
  std::unique_ptr<stubs::InspectArchiveBase> inspect_server_;
};

TEST_F(CollectInspectDataTest, Succeed_AllInspectData) {
  SetUpInspectServer(std::make_unique<stubs::InspectArchive>(
      std::make_unique<stubs::InspectBatchIterator>(std::vector<std::vector<std::string>>({
          {"foo1", "foo2"},
          {"bar1"},
          {},
      }))));

  ::fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& inspect = result.value();
  ASSERT_STREQ(inspect.c_str(), R"([
foo1,
foo2,
bar1
])");

  EXPECT_FALSE(did_timeout_);
}

TEST_F(CollectInspectDataTest, Succeed_PartialInspectData) {
  SetUpInspectServer(std::make_unique<stubs::InspectArchive>(
      std::make_unique<stubs::InspectBatchIteratorNeverRespondsAfterOneBatch>(
          std::vector<std::string>({"foo1", "foo2"}))));

  ::fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& inspect = result.value();
  ASSERT_STREQ(inspect.c_str(), R"([
foo1,
foo2
])");

  EXPECT_TRUE(did_timeout_);
}

TEST_F(CollectInspectDataTest, Fail_NoInspectData) {
  SetUpInspectServer(std::make_unique<stubs::InspectArchive>(
      std::make_unique<stubs::InspectBatchIterator>(std::vector<std::vector<std::string>>({{}}))));

  ::fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  EXPECT_FALSE(did_timeout_);
}

TEST_F(CollectInspectDataTest, Fail_BatchIteratorReturnsError) {
  SetUpInspectServer(std::make_unique<stubs::InspectArchive>(
      std::make_unique<stubs::InspectBatchIteratorReturnsError>()));

  ::fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());
  EXPECT_FALSE(did_timeout_);
}

TEST_F(CollectInspectDataTest, Fail_BatchIteratorNeverResponds) {
  SetUpInspectServer(std::make_unique<stubs::InspectArchive>(
      std::make_unique<stubs::InspectBatchIteratorNeverResponds>()));

  ::fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  EXPECT_TRUE(did_timeout_);
}

TEST_F(CollectInspectDataTest, Fail_ArchiveClosesIteratorClosesConnection) {
  SetUpInspectServer(std::make_unique<stubs::InspectArchiveClosesIteratorConnection>());

  ::fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  EXPECT_FALSE(did_timeout_);
}

}  // namespace
}  // namespace feedback
