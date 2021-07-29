// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/board_info_provider.h"

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <map>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/stubs/board_info_provider.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::hwinfo::BoardInfo;
using fxl::SplitResult::kSplitWantNonEmpty;
using fxl::WhiteSpaceHandling::kTrimWhitespace;
using sys::testing::ServiceDirectoryProvider;
using testing::ElementsAreArray;
using testing::IsEmpty;
using testing::Pair;

class BoardInfoProviderTest : public UnitTestFixture {
 public:
  BoardInfoProviderTest() : executor_(dispatcher()) {}

 protected:
  void SetUpBoardProviderServer(std::unique_ptr<stubs::BoardInfoProviderBase> server) {
    board_provider_server_ = std::move(server);
    if (board_provider_server_) {
      InjectServiceProvider(board_provider_server_.get());
    }
  }

  Annotations GetBoardInfo(const AnnotationKeys& allowlist = {},
                           const zx::duration timeout = zx::sec(1)) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    cobalt::Logger cobalt(dispatcher(), services(), &clock_);

    BoardInfoProvider provider(dispatcher(), services(), &cobalt);
    auto promise = provider.GetAnnotations(timeout, allowlist);

    Annotations annotations;
    executor_.schedule_task(
        std::move(promise).then([&annotations](::fpromise::result<Annotations>& res) {
          if (res.is_ok()) {
            annotations = res.take_value();
          }
        }));
    RunLoopFor(timeout);

    return annotations;
  }

  async::Executor executor_;

 private:
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::BoardInfoProviderBase> board_provider_server_;
};

BoardInfo CreateBoardInfo(const std::map<AnnotationKey, std::string>& annotations) {
  BoardInfo info;

  for (const auto& [key, value] : annotations) {
    if (key == kAnnotationHardwareBoardName) {
      info.set_name(value);
    } else if (key == kAnnotationHardwareBoardRevision) {
      info.set_revision(value);
    }
  }

  return info;
}

TEST_F(BoardInfoProviderTest, Succeed_AllAnnotationsRequested) {
  auto board_provider = std::make_unique<stubs::BoardInfoProvider>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
      {kAnnotationHardwareBoardRevision, "some-revision"},
  }));
  SetUpBoardProviderServer(std::move(board_provider));

  auto board_info = GetBoardInfo(/*allowlist=*/{
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
  });
  EXPECT_THAT(board_info, ElementsAreArray({
                              Pair(kAnnotationHardwareBoardName, "some-name"),
                              Pair(kAnnotationHardwareBoardRevision, "some-revision"),
                          }));
}

TEST_F(BoardInfoProviderTest, Succeed_SingleAnnotationRequested) {
  auto board_provider = std::make_unique<stubs::BoardInfoProvider>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
      {kAnnotationHardwareBoardRevision, "some-revision"},
  }));
  SetUpBoardProviderServer(std::move(board_provider));

  auto board_info = GetBoardInfo(/*allowlist=*/{
      kAnnotationHardwareBoardName,
  });
  EXPECT_THAT(board_info, ElementsAreArray({
                              Pair(kAnnotationHardwareBoardName, "some-name"),
                          }));
}

TEST_F(BoardInfoProviderTest, Succeed_SpuriousAnnotationRequested) {
  auto board_provider = std::make_unique<stubs::BoardInfoProvider>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
      {kAnnotationHardwareBoardRevision, "some-revision"},
  }));
  SetUpBoardProviderServer(std::move(board_provider));

  auto board_info = GetBoardInfo(/*allowlist=*/{
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
      "bad-key",
  });
  EXPECT_THAT(board_info, ElementsAreArray({
                              Pair(kAnnotationHardwareBoardName, "some-name"),
                              Pair(kAnnotationHardwareBoardRevision, "some-revision"),
                          }));
}

TEST_F(BoardInfoProviderTest, Succeed_SingleAnnotationInResponse) {
  auto board_provider = std::make_unique<stubs::BoardInfoProvider>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
  }));
  SetUpBoardProviderServer(std::move(board_provider));

  auto board_info = GetBoardInfo(/*allowlist=*/{
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
  });
  EXPECT_THAT(board_info,
              ElementsAreArray({
                  Pair(kAnnotationHardwareBoardName, AnnotationOr("some-name")),
                  Pair(kAnnotationHardwareBoardRevision, AnnotationOr(Error::kMissingValue)),
              }));
}

TEST_F(BoardInfoProviderTest, Succeed_NoRequestedKeysInAllowlist) {
  auto board_provider = std::make_unique<stubs::BoardInfoProvider>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
      {kAnnotationHardwareBoardRevision, "some-revision"},
  }));
  SetUpBoardProviderServer(std::move(board_provider));

  auto board_info = GetBoardInfo(/*allowlist=*/{
      "not-returned-by-board-provider",
  });
  EXPECT_THAT(board_info, IsEmpty());
}

TEST_F(BoardInfoProviderTest, Check_CobaltLogsTimeout) {
  SetUpBoardProviderServer(std::make_unique<stubs::BoardInfoProviderNeverReturns>());

  auto board_info = GetBoardInfo(/*allowlist=*/{
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
  });

  EXPECT_THAT(board_info, ElementsAreArray({
                              Pair(kAnnotationHardwareBoardName, Error::kTimeout),
                              Pair(kAnnotationHardwareBoardRevision, Error::kTimeout),
                          }));
  EXPECT_THAT(ReceivedCobaltEvents(), ElementsAreArray({
                                          cobalt::Event(cobalt::TimedOutData::kBoardInfo),
                                      }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
