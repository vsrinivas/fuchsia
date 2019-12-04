// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/board_info_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <map>
#include <memory>
#include <string>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/tests/stub_board.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::hwinfo::BoardInfo;
using fxl::SplitResult::kSplitWantNonEmpty;
using fxl::WhiteSpaceHandling::kTrimWhitespace;
using sys::testing::ServiceDirectoryProvider;
using testing::Pair;
using testing::UnorderedElementsAreArray;

class BoardInfoProviderTest : public UnitTestFixture {
 public:
  BoardInfoProviderTest() : executor_(dispatcher()) {}

 protected:
  void SetUpBoardProvider(std::unique_ptr<StubBoard> board_provider) {
    board_provider_ = std::move(board_provider);
    if (board_provider_) {
      InjectServiceProvider(board_provider_.get());
    }
  }

  std::map<std::string, std::string> GetBoardInfo(const std::set<std::string>& annotations_to_get,
                                                  const zx::duration timeout = zx::sec(1)) {
    BoardInfoProvider provider(annotations_to_get, dispatcher(), services(), timeout);

    auto promise = provider.GetAnnotations();

    std::vector<Annotation> annotations;
    executor_.schedule_task(
        std::move(promise).then([&annotations](fit::result<std::vector<Annotation>>& res) {
          if (res.is_ok()) {
            annotations = res.take_value();
          }
        }));
    RunLoopFor(timeout);

    if (annotations.empty()) {
      return {};
    }

    std::map<std::string, std::string> board_info;
    for (auto& annotation : annotations) {
      board_info[annotation.key] = std::move(annotation.value);
    }

    return board_info;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<StubBoard> board_provider_;
};

BoardInfo CreateBoardInfo(const std::map<std::string, std::string>& annotations) {
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
  auto board_provider = std::make_unique<StubBoard>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
      {kAnnotationHardwareBoardRevision, "some-revision"},
  }));
  SetUpBoardProvider(std::move(board_provider));

  auto board_info = GetBoardInfo({
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
  });
  EXPECT_THAT(board_info, UnorderedElementsAreArray({
                              Pair(kAnnotationHardwareBoardName, "some-name"),
                              Pair(kAnnotationHardwareBoardRevision, "some-revision"),
                          }));
}

TEST_F(BoardInfoProviderTest, Succeed_SingleAnnotationRequested) {
  auto board_provider = std::make_unique<StubBoard>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
  }));
  SetUpBoardProvider(std::move(board_provider));

  auto board_info = GetBoardInfo({
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
  });
  EXPECT_THAT(board_info, UnorderedElementsAreArray({
                              Pair(kAnnotationHardwareBoardName, "some-name"),
                          }));
}

TEST_F(BoardInfoProviderTest, Succeed_SpuriousAnnotationRequested) {
  auto board_provider = std::make_unique<StubBoard>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
      {kAnnotationHardwareBoardRevision, "some-revision"},
  }));
  SetUpBoardProvider(std::move(board_provider));

  auto board_info = GetBoardInfo({
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
      "bad-key",
  });
  EXPECT_THAT(board_info, UnorderedElementsAreArray({
                              Pair(kAnnotationHardwareBoardName, "some-name"),
                              Pair(kAnnotationHardwareBoardRevision, "some-revision"),
                          }));
}

TEST_F(BoardInfoProviderTest, Succeed_MissingAnnotationReturned) {
  auto board_provider = std::make_unique<StubBoard>(CreateBoardInfo({
      {kAnnotationHardwareBoardName, "some-name"},
  }));
  SetUpBoardProvider(std::move(board_provider));

  auto board_info = GetBoardInfo({
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
  });
  EXPECT_THAT(board_info, UnorderedElementsAreArray({
                              Pair(kAnnotationHardwareBoardName, "some-name"),
                          }));
}

TEST_F(BoardInfoProviderTest, Fail_CallGetBoardInfoTwice) {
  SetUpBoardProvider(std::make_unique<StubBoard>(CreateBoardInfo({})));

  const zx::duration unused_timeout = zx::sec(1);
  internal::BoardInfoPtr board_info_ptr(dispatcher(), services());
  executor_.schedule_task(board_info_ptr.GetBoardInfo(unused_timeout));
  ASSERT_DEATH(board_info_ptr.GetBoardInfo(unused_timeout),
               testing::HasSubstr("GetBoardInfo() is not intended to be called twice"));
}

}  // namespace
}  // namespace feedback
