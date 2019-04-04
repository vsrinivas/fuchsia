// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/feedback_agent.h"

#include <stdint.h>

#include <memory>
#include <ostream>
#include <vector>

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fostr/fidl/fuchsia/math/formatting.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/vector.h>
#include <src/lib/fxl/logging.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>

#include "src/lib/files/file.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

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

struct RGBA {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

// Returns an 8-bit BGRA image of a |image_dim_in_px| x |image_dim_in_px|
// checkerboard, where each white/black region is a 10x10 pixel square.
ScreenshotData CreateCheckerboardScreenshot(const size_t image_dim_in_px) {
  const size_t height = image_dim_in_px;
  const size_t width = image_dim_in_px;
  const size_t block_size = 10;
  const uint8_t black = 0;
  const uint8_t white = 0xff;

  const size_t size_in_bytes = image_dim_in_px * image_dim_in_px * sizeof(RGBA);
  auto ptr = std::make_unique<uint8_t[]>(size_in_bytes);
  RGBA* pixels = reinterpret_cast<RGBA*>(ptr.get());

  // We go pixel by pixel, row by row. |y| tracks the row and |x| the column.
  //
  // We compute in which |block_size| x |block_size| block the pixel is to
  // determine the color (black or white). |block_y| tracks the "block" row and
  // |block_x| the "block" column.
  for (size_t y = 0; y < height; ++y) {
    size_t block_y = y / block_size;
    for (size_t x = 0; x < width; ++x) {
      size_t block_x = x / block_size;
      uint8_t block_color = (block_x + block_y) % 2 ? black : white;
      size_t index = y * width + x;
      auto& p = pixels[index];
      p.r = p.g = p.b = block_color;
      p.a = 255;
    }
  }

  ScreenshotData screenshot;
  FXL_CHECK(zx::vmo::create(size_in_bytes, 0u, &screenshot.data.vmo) == ZX_OK);
  FXL_CHECK(screenshot.data.vmo.write(ptr.get(), 0u, size_in_bytes) == ZX_OK);
  screenshot.data.size = size_in_bytes;
  screenshot.info.height = image_dim_in_px;
  screenshot.info.width = image_dim_in_px;
  screenshot.info.stride = image_dim_in_px * 4u /*4 bytes per pixel*/;
  screenshot.info.pixel_format = fuchsia::images::PixelFormat::BGRA_8;
  return screenshot;
}

// Returns an empty screenshot with a pixel format different from BGRA-8.
ScreenshotData CreateNonBGRA8Screenshot() {
  ScreenshotData screenshot = CreateEmptyScreenshot();
  screenshot.info.pixel_format = fuchsia::images::PixelFormat::YUY2;
  return screenshot;
}

// Returns a Screenshot with the right dimensions, no image.
std::unique_ptr<Screenshot> MakeUniqueScreenshot(const size_t image_dim_in_px) {
  std::unique_ptr<Screenshot> screenshot = std::make_unique<Screenshot>();
  screenshot->dimensions_in_px.height = image_dim_in_px;
  screenshot->dimensions_in_px.width = image_dim_in_px;
  return screenshot;
}

// Represents arguments for Scenic::TakeScreenshot().
struct TakeScreenshotResponse {
  ScreenshotData screenshot;
  bool success;

  TakeScreenshotResponse(ScreenshotData data, bool success)
      : screenshot(std::move(data)), success(success){};
};

// Represents arguments for DataProvider::GetScreenshotCallback.
struct GetScreenshotResponse {
  std::unique_ptr<Screenshot> screenshot;
};

// Compares two GetScreenshotResponse.
template <typename ResultListenerT>
bool DoGetScreenshotResponseMatch(const GetScreenshotResponse& actual,
                                  const GetScreenshotResponse& expected,
                                  ResultListenerT* result_listener) {
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

  if (!fidl::Equals(actual.screenshot->dimensions_in_px,
      expected.screenshot->dimensions_in_px)) {
    *result_listener << "Expected screenshot dimensions "
                     << expected.screenshot->dimensions_in_px << ", got "
                     << actual.screenshot->dimensions_in_px;
    return false;
  }

  // We do not compare the VMOs.

  return true;
}

// gMock matcher for comparing two GetScreenshotResponse.
MATCHER_P(MatchesGetScreenshotResponse, expected, "") {
  return DoGetScreenshotResponseMatch(arg, expected, result_listener);
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
           "testing GetScreenshot() using set_scenic_responses()";
    TakeScreenshotResponse response = std::move(take_screenshot_responses_[0]);
    take_screenshot_responses_.erase(take_screenshot_responses_.begin());
    callback(std::move(response.screenshot), response.success);
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
    ASSERT_EQ(ZX_OK, context_provider_.service_directory_provider()->AddService(
                         stub_scenic_->GetHandler()));
    agent_.reset(new FeedbackAgent(context_provider_.context()));
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
  ::sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(FeedbackAgentTest, GetScreenshot_SucceedOnScenicReturningSuccess) {
  const size_t image_dim_in_px = 100;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px),
                                kSuccess);
  set_scenic_responses(std::move(scenic_responses));

  GetScreenshotResponse feedback_response;
  agent_->GetScreenshot(
      ImageEncoding::PNG,
      [&feedback_response](std::unique_ptr<Screenshot> screenshot) {
        feedback_response.screenshot = std::move(screenshot);
      });
  RunLoopUntilIdle();

  EXPECT_TRUE(get_scenic_responses().empty());

  ASSERT_NE(feedback_response.screenshot, nullptr);
  EXPECT_EQ((size_t)feedback_response.screenshot->dimensions_in_px.height,
            image_dim_in_px);
  EXPECT_EQ((size_t)feedback_response.screenshot->dimensions_in_px.width,
            image_dim_in_px);
  EXPECT_TRUE(feedback_response.screenshot->image.vmo.is_valid());

  fsl::SizedVmo expected_sized_vmo;
  ASSERT_TRUE(fsl::VmoFromFilename("/pkg/data/checkerboard_100.png",
                                   &expected_sized_vmo));
  std::vector<uint8_t> expected_pixels;
  ASSERT_TRUE(fsl::VectorFromVmo(expected_sized_vmo, &expected_pixels));
  std::vector<uint8_t> actual_pixels;
  ASSERT_TRUE(
      fsl::VectorFromVmo(feedback_response.screenshot->image, &actual_pixels));
  EXPECT_EQ(actual_pixels, expected_pixels);
}

TEST_F(FeedbackAgentTest, GetScreenshot_FailOnScenicReturningFailure) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  set_scenic_responses(std::move(scenic_responses));

  GetScreenshotResponse feedback_response;
  agent_->GetScreenshot(
      ImageEncoding::PNG,
      [&feedback_response](std::unique_ptr<Screenshot> screenshot) {
        feedback_response.screenshot = std::move(screenshot);
      });
  RunLoopUntilIdle();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(FeedbackAgentTest,
       GetScreenshot_FailOnScenicReturningNonBGRA8Screenshot) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateNonBGRA8Screenshot(), kSuccess);
  set_scenic_responses(std::move(scenic_responses));

  GetScreenshotResponse feedback_response;
  agent_->GetScreenshot(
      ImageEncoding::PNG,
      [&feedback_response](std::unique_ptr<Screenshot> screenshot) {
        feedback_response.screenshot = std::move(screenshot);
      });
  RunLoopUntilIdle();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(FeedbackAgentTest, GetScreenshot_ParallelRequests) {
  // We simulate three calls to FeedbackAgent::GetScreenshot(): one for which
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

  std::vector<GetScreenshotResponse> feedback_responses;
  for (size_t i = 0; i < num_calls; i++) {
    agent_->GetScreenshot(
        ImageEncoding::PNG,
        [&feedback_responses](std::unique_ptr<Screenshot> screenshot) {
          feedback_responses.push_back({std::move(screenshot)});
        });
  }
  RunLoopUntilIdle();

  EXPECT_TRUE(get_scenic_responses().empty());

  // We cannot assume that the order of the FeedbackAgent::GetScreenshot()
  // calls match the order of the Scenic::TakeScreenshot() callbacks because of
  // the async message loop. Thus we need to match them as sets.
  //
  // We set the expectations in advance and then pass a reference to the gMock
  // matcher using testing::ByRef() because the underlying VMO is not copyable.
  const GetScreenshotResponse expected_0 = {
      MakeUniqueScreenshot(image_dim_in_px_0)};
  const GetScreenshotResponse expected_1 = {
      MakeUniqueScreenshot(image_dim_in_px_1)};
  const GetScreenshotResponse expected_2 = {nullptr};
  EXPECT_THAT(feedback_responses,
              testing::UnorderedElementsAreArray({
                  MatchesGetScreenshotResponse(testing::ByRef(expected_0)),
                  MatchesGetScreenshotResponse(testing::ByRef(expected_1)),
                  MatchesGetScreenshotResponse(testing::ByRef(expected_2)),
              }));

  // Additionally, we check that in the non-empty responses, the VMO is valid.
  for (const auto& response : feedback_responses) {
    if (response.screenshot == nullptr) {
      continue;
    }
    EXPECT_TRUE(response.screenshot->image.vmo.is_valid());
    EXPECT_GE(response.screenshot->image.size, 0u);
  }
}

TEST_F(FeedbackAgentTest, GetData_SmokeTest) {
  DataProvider_GetData_Result feedback_result;
  agent_->GetData([&feedback_result](DataProvider_GetData_Result result) {
    feedback_result = std::move(result);
  });
  RunLoopUntilIdle();
  ASSERT_TRUE(feedback_result.is_response());
  // There is nothing else we can assert here as no missing annotation nor
  // attachment is fatal.
}

}  // namespace
}  // namespace feedback
}  // namespace fuchsia

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback_agent", "test"});
  return RUN_ALL_TESTS();
}
