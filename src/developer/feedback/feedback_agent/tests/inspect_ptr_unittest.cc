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
#include "src/developer/feedback/feedback_agent/tests/stub_inspect_archive.h"
#include "src/developer/feedback/feedback_agent/tests/stub_inspect_batch_iterator.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

class CollectInspectDataTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  CollectInspectDataTest()
      : CobaltTestFixture(/*unit_test_fixture=*/this), executor_(dispatcher()) {}

 protected:
  void SetUpInspect(std::unique_ptr<StubInspectArchive> inspect_archive) {
    inspect_archive_ = std::move(inspect_archive);
    if (inspect_archive_) {
      InjectServiceProvider(inspect_archive_.get());
    }
  }

  fit::result<AttachmentValue> CollectInspectData(const zx::duration timeout = zx::sec(1)) {
    SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());
    Cobalt cobalt(dispatcher(), services());

    fit::result<AttachmentValue> result;
    executor_.schedule_task(
        feedback::CollectInspectData(dispatcher(), services(), timeout, &cobalt)
            .then([&result](fit::result<AttachmentValue>& res) { result = std::move(res); }));
    RunLoopFor(timeout);
    return result;
  }

  void CheckNoTimeout() { EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty()); }

  void CheckTimeout() {
    EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                            CobaltEvent(TimedOutData::kInspect),
                                        }));
  }

  async::Executor executor_;

 private:
  std::unique_ptr<StubInspectArchive> inspect_archive_;
};

TEST_F(CollectInspectDataTest, Succeed_AllInspectData) {
  SetUpInspect(std::make_unique<StubInspectArchive>(
      std::make_unique<StubInspectBatchIterator>(std::vector<std::vector<std::string>>({
          {"foo1", "foo2"},
          {"bar1"},
          {},
      }))));

  fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& inspect = result.value();
  ASSERT_STREQ(inspect.c_str(), R"([
foo1,
foo2,
bar1
])");

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Succeed_PartialInspectData) {
  SetUpInspect(std::make_unique<StubInspectArchive>(
      std::make_unique<StubInspectBatchIteratorNeverRespondsAfterOneBatch>(
          std::vector<std::string>({"foo1", "foo2"}))));

  fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& inspect = result.value();
  ASSERT_STREQ(inspect.c_str(), R"([
foo1,
foo2
])");

  CheckTimeout();
}

TEST_F(CollectInspectDataTest, Fail_NoInspectData) {
  SetUpInspect(std::make_unique<StubInspectArchive>(
      std::make_unique<StubInspectBatchIterator>(std::vector<std::vector<std::string>>({{}}))));

  fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_BatchIteratorReturnsError) {
  SetUpInspect(std::make_unique<StubInspectArchive>(
      std::make_unique<StubInspectBatchIteratorReturnsError>()));

  fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());
  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_BatchIteratorNeverResponds) {
  SetUpInspect(std::make_unique<StubInspectArchive>(
      std::make_unique<StubInspectBatchIteratorNeverResponds>()));

  fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckTimeout();
}

TEST_F(CollectInspectDataTest, Fail_ArchiveClosesIteratorClosesConnection) {
  SetUpInspect(std::make_unique<StubInspectArchiveClosesIteratorConnection>());

  fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_ArchiveClosesConnection) {
  SetUpInspect(std::make_unique<StubInspectArchiveClosesArchiveConnection>());

  fit::result<AttachmentValue> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_CallCollectTwice) {
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());
  Cobalt cobalt(dispatcher(), services());

  const zx::duration unused_timeout = zx::sec(1);
  Inspect inspect(dispatcher(), services(), &cobalt);
  executor_.schedule_task(inspect.Collect(unused_timeout));
  ASSERT_DEATH(inspect.Collect(unused_timeout),
               testing::HasSubstr("Collect() is not intended to be called twice"));
}

}  // namespace
}  // namespace feedback
