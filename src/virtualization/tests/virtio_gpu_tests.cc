// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <test/inputsynthesis/cpp/fidl.h>

#include "lib/zx/clock.h"
#include "lib/zx/time.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/tests/lib/enclosed_guest.h"
#include "src/virtualization/tests/lib/guest_test.h"
#include "src/virtualization/tests/lib/periodic_logger.h"

namespace {

using testing::AllOf;
using testing::Ge;
using testing::Le;

// Set to "true" to save screenshots to disk.
constexpr bool kSaveScreenshot = false;
constexpr char kScreenshotSaveLocation[] = "/tmp/screenshot-%s.raw";

constexpr char kVirtioGpuTestUtil[] = "virtio_gpu_test_util";

// How long to run tests before giving up and failing.
constexpr zx::duration kGpuTestTimeout = zx::sec(15);

struct Screenshot {
  // Height and width of the image, in pixels.
  int height;
  int width;

  // Raw pixel data, 4 bytes per pixel, stored in RGBO format, one row at
  // a time.
  std::vector<std::byte> data;
};

template <typename T>
class VirtioGpuTest : public GuestTest<T> {
 protected:
  void SetUp() override {
    GuestTest<T>::SetUp();
    screenshot_ =
        this->GetEnclosedGuest().template ConnectToService<fuchsia::ui::composition::Screenshot>();
    display_info_ = this->GetEnclosedGuest().WaitForDisplay();
  }

  zx_status_t CaptureScreenshot(Screenshot* screenshot) {
    fuchsia::ui::composition::ScreenshotTakeRequest request;
    request.set_format(fuchsia::ui::composition::ScreenshotFormat::BGRA_RAW);
    std::optional<zx_status_t> screenshot_result;
    screenshot_->Take(std::move(request), [screenshot, &screenshot_result](auto response) {
      screenshot->width = response.size().width;
      screenshot->height = response.size().height;

      size_t size = screenshot->width * screenshot->height * 4;
      screenshot->data.reserve(size);
      zx_status_t status = response.vmo().read(screenshot->data.data(), 0, size);
      if (status != ZX_OK) {
        screenshot_result = status;
      }
      screenshot_result = ZX_OK;
    });

    if (!this->RunLoopUntil([&] { return screenshot_result.has_value(); },
                            zx::deadline_after(kGpuTestTimeout))) {
      return ZX_ERR_TIMED_OUT;
    }
    return *screenshot_result;
  }

  EnclosedGuest::DisplayInfo display_info_;
  fuchsia::ui::composition::ScreenshotPtr screenshot_;
};

using GuestTypes = ::testing::Types<DebianGpuEnclosedGuest, ZirconGpuEnclosedGuest>;
TYPED_TEST_SUITE(VirtioGpuTest, GuestTypes, GuestTestNameGenerator);

// Poll |condition| using exponential backoff until it returns true, or
// a timeout has passed.
//
// Returns true iff |condition| returned true before the deadline.
template <typename C>
bool PollCondition(C condition, zx::duration timeout, PeriodicLogger logger) {
  const zx::time deadline = zx::deadline_after(timeout);
  zx::duration wait_time = zx::usec(1);

  do {
    // Have we succeeded?
    if (condition()) {
      return true;
    }

    // Sleep, with exponential backoff.
    zx::nanosleep(zx::deadline_after(wait_time));
    wait_time = std::max(wait_time * 2, zx::sec(1));
  } while (zx::clock::get_monotonic() < deadline);

  // Perform one final check.
  return condition();
}

// compiled in.
void SaveScreenshot(const std::string& prefix, const Screenshot& screenshot) {
  if (kSaveScreenshot) {
    std::string filename = fxl::StringPrintf(kScreenshotSaveLocation, prefix.c_str());
    FX_LOGS(INFO) << fxl::StringPrintf(
        "Saving screenshot to '%s'. Copy from the device using:\n"
        "#  fx scp \"[$(fx get-device-addr)]\":%s data.raw\n"
        "Display it using ImageMagick using one of the following commands.\n"
        "Linux guests:\n"
        "#  display -size %dx%d -depth 8 RGBO:data.raw\n"
        "Fuchsia guests:\n"
        "#  display -size %dx%d -depth 8 RGBA:data.raw\n",
        filename.c_str(), filename.c_str(), screenshot.width, screenshot.height, screenshot.width,
        screenshot.height);
    std::ofstream output;
    output.open(filename);
    output.write(reinterpret_cast<const char*>(screenshot.data.data()), screenshot.data.size());
    output.close();
  }
}

// Determine if the given screenshot has at least 1 non-black pixel.
//
// We assume the data format is RGBA or RGBO, where each pixel is four bytes:
// [red] [green] [blue] [alpha/opacity]
bool HasNonBlackPixel(const Screenshot& screenshot) {
  FX_CHECK(screenshot.data.size() % 4 == 0);
  for (size_t i = 0; i < screenshot.data.size(); i += 4) {
    std::byte r = screenshot.data[i + 0];
    std::byte g = screenshot.data[i + 1];
    std::byte b = screenshot.data[i + 2];
    if (r != std::byte(0) || g != std::byte(0) || b != std::byte(0)) {
      return true;
    }
  }
  return false;
}

// Count number of unique colours in the screenshot.
//
// For this test, we treat data as having different alpha values as different
// colours.
int NumberOfUniqueColors(const Screenshot& screenshot) {
  FX_CHECK(screenshot.data.size() % 4 == 0);
  std::unordered_set<uint32_t> seen_colors;
  int unique_colors = 0;
  for (size_t i = 0; i < screenshot.data.size(); i += 4) {
    uint32_t color = *reinterpret_cast<const uint32_t*>(&screenshot.data[i]);
    auto [_, new_color] = seen_colors.insert(color);
    if (new_color) {
      unique_colors++;
    }
  }
  return unique_colors;
}

bool ScreenshotsSame(const Screenshot& a, const Screenshot& b) {
  if (a.height != b.height || a.width != b.width) {
    return false;
  }
  return a.data == b.data;
}

TYPED_TEST(VirtioGpuTest, DetectDisplay) {
  std::string result;
  ASSERT_EQ(this->RunUtil(kVirtioGpuTestUtil, {"detect"}, &result), ZX_OK);

  // Expect that a single display was detected, and the geometry should match that of the created
  // view.
  //
  // The output is expected to look like:
  // {
  //   "displays": [
  //     {
  //       "width": <width>,
  //       "height": <height>
  //     }
  //    ]
  // }
  //
  // The width and height are expected to also match the size of the backing Fuchsia view.
  rapidjson::Document document;
  document.Parse(result);
  EXPECT_TRUE(document.IsObject());
  EXPECT_TRUE(document.HasMember("displays"));
  EXPECT_TRUE(document["displays"].IsArray());

  const auto& displays = document["displays"].GetArray();
  EXPECT_EQ(1u, displays.Size());
  EXPECT_TRUE(displays[0].IsObject());

  const auto& display = displays[0];
  EXPECT_TRUE(display.HasMember("width") && display["width"].IsUint());
  EXPECT_TRUE(display.HasMember("height") && display["height"].IsUint());
  EXPECT_EQ(this->display_info_.width, display["width"].GetUint());
  EXPECT_EQ(this->display_info_.height, display["height"].GetUint());
}

TYPED_TEST(VirtioGpuTest, ScreenNotBlack) {
  // TODO(fxbug.dev/102870): Revive the this test.
  GTEST_SKIP();

  // Take a screenshot.
  Screenshot screenshot;
  zx_status_t status = this->CaptureScreenshot(&screenshot);
  ASSERT_EQ(status, ZX_OK) << "Error capturing screenshot.";
  SaveScreenshot("screen-not-black", screenshot);

  // Ensure that at least 1 pixel is not black.
  EXPECT_TRUE(HasNonBlackPixel(screenshot)) << "All pixels in the captured screenshot were black.";
}

TYPED_TEST(VirtioGpuTest, ScreenDataLooksValid) {
  // TODO(fxbug.dev/102870): Revive the this test.
  GTEST_SKIP();

  // Take a screenshot.
  Screenshot screenshot;
  zx_status_t status = this->CaptureScreenshot(&screenshot);
  ASSERT_EQ(status, ZX_OK) << "Error capturing screenshot.";
  SaveScreenshot("unique-colors", screenshot);

  // Ensure that we have at least 2 distinct colours, but no more than 16. The
  // idea here is that we want to ensure the screen is showing _something_
  // (white text on a black background), but not complete garbage
  // (uninitialised memory, for example).
  //
  // Both Zircon and Linux guests have a simple console on bootup, so ensuring
  // that we only have a few unique colours lets us approximate this.
  //
  // If you've just added a beautiful rainbow to Fuchsia's console and now
  // this test is failing, I'm really, truly sorry.
  EXPECT_THAT(NumberOfUniqueColors(screenshot), AllOf(Ge(2), Le(16)))
      << "The screenshot had a suspicious number of colours, suggesting it "
         "may not actually be real screen content.";
}

TYPED_TEST(VirtioGpuTest, TextInputChangesConsole) {
  // TODO(fxbug.dev/102870): Revive the this test.
  GTEST_SKIP();

  // Take a screenshot.
  Screenshot screenshot1;
  zx_status_t status = this->CaptureScreenshot(&screenshot1);
  ASSERT_EQ(status, ZX_OK) << "Error capturing screenshot.";
  SaveScreenshot("input-state1", screenshot1);

  // Type a key, which should update the display.
  auto input_synthesis =
      this->GetEnclosedGuest().template ConnectToService<test::inputsynthesis::Text>();
  bool input_injected = false;
  input_synthesis->Send("a", [&input_injected]() { input_injected = true; });
  this->RunLoopUntil([&] { return input_injected; }, zx::deadline_after(kGpuTestTimeout));

  // Take another screenshot.
  //
  // We keep polling to handle any delay in propagating input to output.
  bool success = PollCondition(
      [&screenshot1]() -> bool {
        // Wait for the screen to change.
        Screenshot screenshot2;
        SaveScreenshot("input-state2", screenshot2);
        return !ScreenshotsSame(screenshot1, screenshot2);
      },
      /*timeout=*/kGpuTestTimeout, PeriodicLogger("Waiting for change in console", zx::sec(1)));

  // Ensure something changed.
  EXPECT_TRUE(success)
      << "Expected keystroke events to change console output, but nothing changed.";
}

}  // namespace
