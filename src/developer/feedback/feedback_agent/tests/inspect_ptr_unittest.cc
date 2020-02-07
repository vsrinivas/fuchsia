// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/inspect_ptr.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/tests/stub_inspect_archive.h"
#include "src/developer/feedback/feedback_agent/tests/stub_inspect_batch_iterator.h"
#include "src/developer/feedback/feedback_agent/tests/stub_inspect_reader.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/lib/fsl/vmo/strings.h"
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

  fit::result<fuchsia::mem::Buffer> CollectInspectData(const zx::duration timeout = zx::sec(1)) {
    Cobalt cobalt(dispatcher(), services());

    fit::result<fuchsia::mem::Buffer> result;
    executor_.schedule_task(
        feedback::CollectInspectData(dispatcher(), services(), timeout, &cobalt)
            .then([&result](fit::result<fuchsia::mem::Buffer>& res) { result = std::move(res); }));
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
  SetUpInspect(std::make_unique<StubInspectArchive>(std::make_unique<StubInspectReader>(
      std::make_unique<StubInspectBatchIterator>(std::vector<std::vector<std::string>>({
          {"foo1", "foo2"},
          {"bar1"},
          {},
      })))));
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_ok());

  const fuchsia::mem::Buffer& inspect = result.value();
  std::string inspect_json;
  ASSERT_TRUE(fsl::StringFromVmo(inspect, &inspect_json));
  ASSERT_STREQ(inspect_json.c_str(), R"([
foo1,
foo2,
bar1
])");

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Succeed_PartialInspectData) {
  SetUpInspect(std::make_unique<StubInspectArchive>(std::make_unique<StubInspectReader>(
      std::make_unique<StubInspectBatchIteratorNeverRespondsAfterOneBatch>(
          std::vector<std::string>({"foo1", "foo2"})))));
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_ok());

  const fuchsia::mem::Buffer& inspect = result.value();
  std::string inspect_json;
  ASSERT_TRUE(fsl::StringFromVmo(inspect, &inspect_json));
  ASSERT_STREQ(inspect_json.c_str(), R"([
foo1,
foo2
])");

  CheckTimeout();
}

TEST_F(CollectInspectDataTest, Fail_NoInspectData) {
  SetUpInspect(std::make_unique<StubInspectArchive>(std::make_unique<StubInspectReader>(
      std::make_unique<StubInspectBatchIterator>(std::vector<std::vector<std::string>>({{}})))));
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_BatchIteratorClosesConnection) {
  SetUpInspect(std::make_unique<StubInspectArchive>(
      std::make_unique<StubInspectReaderClosesBatchIteratorConnection>(
          std::make_unique<StubInspectBatchIterator>())));
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_BatchIteratorReturnsError) {
  SetUpInspect(std::make_unique<StubInspectArchive>(std::make_unique<StubInspectReader>(
      std::make_unique<StubInspectBatchIteratorReturnsError>())));
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_BatchIteratorNeverResponds) {
  SetUpInspect(std::make_unique<StubInspectArchive>(std::make_unique<StubInspectReader>(
      std::make_unique<StubInspectBatchIteratorNeverResponds>())));
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckTimeout();
}

TEST_F(CollectInspectDataTest, Fail_ReaderClosesConnection) {
  SetUpInspect(std::make_unique<StubInspectArchiveClosesReaderConnection>());
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_ReaderReturnsError) {
  SetUpInspect(
      std::make_unique<StubInspectArchive>(std::make_unique<StubInspectReaderReturnsError>()));
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_ReaderNeverResponds) {
  SetUpInspect(
      std::make_unique<StubInspectArchive>(std::make_unique<StubInspectReaderNeverResponds>()));
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckTimeout();
}

TEST_F(CollectInspectDataTest, Fail_ArchiveClosesConnection) {
  SetUpInspect(std::make_unique<StubInspectArchiveClosesArchiveConnection>());
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_ArchiveReturnsError) {
  SetUpInspect(std::make_unique<StubInspectArchiveReturnsError>());
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckNoTimeout();
}

TEST_F(CollectInspectDataTest, Fail_ArchiveNeverResponds) {
  SetUpInspect(std::make_unique<StubInspectArchiveNeverResponds>());
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();
  ASSERT_TRUE(result.is_error());

  CheckTimeout();
}

TEST_F(CollectInspectDataTest, Fail_CallCollectTwice) {
  Cobalt cobalt(dispatcher(), services());
  const zx::duration unused_timeout = zx::sec(1);
  Inspect inspect(dispatcher(), services(), &cobalt);
  executor_.schedule_task(inspect.Collect(unused_timeout));
  ASSERT_DEATH(inspect.Collect(unused_timeout),
               testing::HasSubstr("Collect() is not intended to be called twice"));
}

}  // namespace
}  // namespace feedback
