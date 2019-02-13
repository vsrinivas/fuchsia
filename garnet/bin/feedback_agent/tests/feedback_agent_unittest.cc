// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/feedback_agent/feedback_agent.h"

#include <memory>
#include <ostream>
#include <vector>

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lib/escher/util/image_utils.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/logging.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/startup_context_for_test.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>

namespace fuchsia {
namespace feedback {
namespace {

using fuchsia::ui::scenic::ScreenshotData;

constexpr bool kSuccess = true;
constexpr bool kFailure = false;

// Returns an empty screenshot, still needed when Scenic::TakeScreenshot()
// returns false as the FIDL ScreenshotData field is not marked optional in
// fuchsia.ui.scenic.Scenic.TakeScreenshot.
ScreenshotData CreateEmptyScreenshot() {
  ScreenshotData screenshot;
  FXL_CHECK(zx::vmo::create(0, 0u, &screenshot.data.vmo) == ZX_OK);
  return screenshot;
}

// Returns a BGRA image of a checkerboard, where each white/black
// region is a single pixel, |image_dim_in_px| x |image_dim_in_px|.
ScreenshotData CreateCheckerboardScreenshot(const size_t image_dim_in_px) {
  size_t pixels_size;
  std::unique_ptr<uint8_t[]> pixels =
      escher::image_utils::NewCheckerboardPixels(image_dim_in_px,
                                                 image_dim_in_px, &pixels_size);

  ScreenshotData screenshot;
  FXL_CHECK(zx::vmo::create(pixels_size, 0u, &screenshot.data.vmo) == ZX_OK);
  FXL_CHECK(screenshot.data.vmo.write(pixels.get(), 0u, pixels_size) == ZX_OK);
  screenshot.data.size = pixels_size;
  screenshot.info.height = image_dim_in_px;
  screenshot.info.width = image_dim_in_px;
  return screenshot;
}

// Returns a PngImage with the right dimensions, no data.
std::unique_ptr<PngImage> MakeUniquePngImage(const size_t image_dim_in_px) {
  std::unique_ptr<PngImage> image = std::make_unique<PngImage>();
  image->dimensions.height_in_px = image_dim_in_px;
  image->dimensions.width_in_px = image_dim_in_px;
  return image;
}

// Represents arguments for Scenic::TakeScreenshot().
struct TakeScreenshotResponse {
  ScreenshotData screenshot;
  bool success;

  TakeScreenshotResponse(ScreenshotData data, bool success)
      : screenshot(std::move(data)), success(success){};
};

// Represents arguments for DataProvider::GetPngScreenshotCallback.
struct GetPngScreenshotResponse {
  Status status = Status::UNKNOWN;
  std::unique_ptr<PngImage> screenshot;
};

// Compares two GetPngScreenshotResponse.
template <typename ResultListenerT>
bool DoGetPngScreenshotResponseMatch(const GetPngScreenshotResponse& actual,
                                     const GetPngScreenshotResponse& expected,
                                     ResultListenerT* result_listener) {
  if (actual.status != expected.status) {
    *result_listener << "Expected status " << expected.status << ", got "
                     << actual.status;
    return false;
  }

  if (actual.screenshot == nullptr && expected.screenshot == nullptr) {
    return true;
  }
  if (actual.screenshot == nullptr && expected.screenshot != nullptr) {
    *result_listener << "Got no screenshot, expected one";
    return false;
  }
  if (expected.screenshot == nullptr && actual.screenshot != nullptr) {
    *result_listener << "Expected no screenshot, got one";
    return false;
  }
  // actual.screenshot and expected.screenshot are now valid.

  if (actual.screenshot->dimensions != expected.screenshot->dimensions) {
    *result_listener << "Expected screenshot dimensions "
                     << expected.screenshot->dimensions << ", got "
                     << actual.screenshot->dimensions;
    return false;
  }

  // We do not compare the VMOs.

  return true;
}

// gMock matcher for comparing two GetPngScreenshotResponse.
MATCHER_P(MatchesGetPngScreenshotResponse, expected, "") {
  return DoGetPngScreenshotResponseMatch(arg, expected, result_listener);
}

// Stub Scenic service to return canned responses to Scenic::TakeScreenshot().
class StubScenic : public fuchsia::ui::scenic::Scenic {
 public:
  // Returns a request handler for binding to this spy service.
  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // Scenic methods.
  void CreateSession(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener)
      override {
    FXL_NOTIMPLEMENTED();
  }
  void GetDisplayInfo(GetDisplayInfoCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  void GetDisplayOwnershipEvent(
      GetDisplayOwnershipEventCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  void TakeScreenshot(TakeScreenshotCallback callback) override {
    FXL_CHECK(!take_screenshot_responses_.empty())
        << "You need to set up Scenic::TakeScreenshot() responses before "
           "testing GetPngScreenshot() using "
           "set_scenic_responses()";
    TakeScreenshotResponse args = std::move(take_screenshot_responses_[0]);
    take_screenshot_responses_.erase(take_screenshot_responses_.begin());
    callback(std::move(args.screenshot), args.success);
  }

  // Stub injection and verification methods.
  void set_take_screenshot_responses(
      std::vector<TakeScreenshotResponse> responses) {
    take_screenshot_responses_ = std::move(responses);
  }
  const std::vector<TakeScreenshotResponse>& take_screenshot_responses() const {
    return take_screenshot_responses_;
  }

 private:
  std::vector<TakeScreenshotResponse> take_screenshot_responses_;
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
};

// Unit-tests the implementation of the fuchsia.feedback.DataProvider FIDL
// interface.
//
// This does not test the environment service. It directly instantiates the
// class, without connecting through FIDL.
class FeedbackAgentTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    stub_scenic_.reset(new StubScenic());
    context_ = ::sys::testing::StartupContextForTest::Create();
    ASSERT_EQ(ZX_OK, context_->service_directory_for_test()->AddService(
                         stub_scenic_->GetHandler()));
    agent_.reset(new FeedbackAgent(context_.get()));
  }

 protected:
  void set_scenic_responses(std::vector<TakeScreenshotResponse> responses) {
    stub_scenic_->set_take_screenshot_responses(std::move(responses));
  }
  const std::vector<TakeScreenshotResponse>& get_scenic_responses() const {
    return stub_scenic_->take_screenshot_responses();
  }

  std::unique_ptr<FeedbackAgent> agent_;

 private:
  std::unique_ptr<StubScenic> stub_scenic_;
  std::unique_ptr<::sys::testing::StartupContextForTest> context_;
};

TEST_F(FeedbackAgentTest, GetPngScreenshot_SucceedOnScenicReturningSuccess) {
  const size_t image_dim_in_px = 10;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px),
                                kSuccess);
  set_scenic_responses(std::move(scenic_responses));

  GetPngScreenshotResponse out;
  agent_->GetPngScreenshot(
      [&out](Status status, std::unique_ptr<PngImage> screenshot) {
        out.status = status;
        out.screenshot = std::move(screenshot);
      });
  RunLoopUntilIdle();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(out.status, Status::OK);
  ASSERT_NE(out.screenshot, nullptr);
  EXPECT_EQ(out.screenshot->dimensions.height_in_px, image_dim_in_px);
  EXPECT_EQ(out.screenshot->dimensions.width_in_px, image_dim_in_px);
  EXPECT_TRUE(out.screenshot->data.vmo.is_valid());
}

TEST_F(FeedbackAgentTest, GetPngScreenshot_FailOnScenicReturningFailure) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  set_scenic_responses(std::move(scenic_responses));

  GetPngScreenshotResponse out;
  agent_->GetPngScreenshot(
      [&out](Status status, std::unique_ptr<PngImage> screenshot) {
        out.status = status;
        out.screenshot = std::move(screenshot);
      });
  RunLoopUntilIdle();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(out.status, Status::ERROR);
  EXPECT_EQ(out.screenshot, nullptr);
}

TEST_F(FeedbackAgentTest, GetPngScreenshot_ParallelRequests) {
  // We simulate three calls to FeedbackAgent::GetPngScreenshot(): one for which
  // the stub Scenic will return a checkerboard 10x10, one for a 20x20 and one
  // failure.
  const size_t num_calls = 3u;
  const size_t image_dim_in_px_0 = 10u;
  const size_t image_dim_in_px_1 = 20u;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px_0),
                                kSuccess);
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px_1),
                                kSuccess);
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  ASSERT_EQ(scenic_responses.size(), num_calls);
  set_scenic_responses(std::move(scenic_responses));

  std::vector<GetPngScreenshotResponse> out;
  for (size_t i = 0; i < num_calls; i++) {
    agent_->GetPngScreenshot(
        [&out](Status status, std::unique_ptr<PngImage> image) {
          out.push_back({status, std::move(image)});
        });
  }
  RunLoopUntilIdle();

  EXPECT_TRUE(get_scenic_responses().empty());

  // We cannot assume that the order of the FeedbackAgent::GetPngScreenshot()
  // calls match the order of the Scenic::TakeScreenshot() callbacks because of
  // the async message loop. Thus we need to match them as sets.
  //
  // We set the expectations in advance and then pass a reference to the gMock
  // matcher using testing::ByRef() because the underlying VMO is not copyable.
  const GetPngScreenshotResponse expected_0 = {
      Status::OK, MakeUniquePngImage(image_dim_in_px_0)};
  const GetPngScreenshotResponse expected_1 = {
      Status::OK, MakeUniquePngImage(image_dim_in_px_1)};
  const GetPngScreenshotResponse expected_2 = {Status::ERROR, nullptr};
  EXPECT_THAT(out,
              testing::UnorderedElementsAreArray({
                  MatchesGetPngScreenshotResponse(testing::ByRef(expected_0)),
                  MatchesGetPngScreenshotResponse(testing::ByRef(expected_1)),
                  MatchesGetPngScreenshotResponse(testing::ByRef(expected_2)),
              }));

  // Additionally, we check that in the OK-status outputs, the VMO is valid.
  for (const auto& args : out) {
    if (args.status != Status::OK) {
      continue;
    }
    ASSERT_NE(args.screenshot, nullptr);
    EXPECT_TRUE(args.screenshot->data.vmo.is_valid());
    EXPECT_GE(args.screenshot->data.size, 0u);
  }
}

}  // namespace

// Pretty-prints status in gTest matchers instead of the default byte string in
// case of failed expectations.
void PrintTo(const Status status, std::ostream* os) { *os << status; }

}  // namespace feedback
}  // namespace fuchsia

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback_agent", "test"});
  return RUN_ALL_TESTS();
}
