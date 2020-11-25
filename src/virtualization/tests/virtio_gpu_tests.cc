// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/zx/clock.h"
#include "lib/zx/time.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/tests/enclosed_guest.h"
#include "src/virtualization/tests/fake_scenic.h"
#include "src/virtualization/tests/guest_test.h"
#include "src/virtualization/tests/periodic_logger.h"

namespace {

using testing::AllOf;
using testing::Ge;
using testing::Le;

// Set to "true" to save screenshots to disk.
constexpr bool kSaveScreenshot = false;
constexpr char kScreenshotSaveLocation[] = "/tmp/screenshot-%s.raw";

// How long to run tests before giving up and failing.
constexpr zx::duration kGpuTestTimeout = zx::sec(15);

template <typename T>
using VirtioGpuTest = GuestTest<T>;

// TODO(fxdebug.dev/64348): Re-enable ZirconEnclosedGuest.
using GuestTypes = ::testing::Types<DebianEnclosedGuest>;
TYPED_TEST_SUITE(VirtioGpuTest, GuestTypes);

// Poll |condition| using exponential backoff until it returns true, or
// a timeout has passed.
//
// Returns true iff |condition| returned true before the deadline.
template <typename C>
bool PollCondition(C condition, zx::duration timeout, std::optional<PeriodicLogger> logger) {
  const zx::time deadline = zx::deadline_after(timeout);
  zx::duration wait_time = zx::usec(1);

  do {
    // Have we succeeded?
    if (condition()) {
      return true;
    }

    logger->LogIfRequired();

    // Sleep, with exponential backoff.
    zx::nanosleep(zx::deadline_after(wait_time));
    wait_time = std::max(wait_time * 2, zx::sec(1));
  } while (zx::clock::get_monotonic() < deadline);

  // Perform one final check.
  return condition();
}

// Save a screenshot to disk, if the constand "kSaveScreeshot" has been
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

TYPED_TEST(VirtioGpuTest, ScreenNotBlack) {
  // Take a screenshot.
  Screenshot screenshot;
  zx_status_t status = this->GetEnclosedGuest()->GetScenic()->CaptureScreenshot(&screenshot);
  ASSERT_EQ(status, ZX_OK) << "Error capturing screenshot.";
  SaveScreenshot("screen-not-black", screenshot);

  // Ensure that at least 1 pixel is not black.
  EXPECT_TRUE(HasNonBlackPixel(screenshot)) << "All pixels in the captured screenshot were black.";
}

TYPED_TEST(VirtioGpuTest, ScreenDataLooksValid) {
  // Take a screenshot.
  Screenshot screenshot;
  zx_status_t status = this->GetEnclosedGuest()->GetScenic()->CaptureScreenshot(&screenshot);
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
  // Take a screenshot.
  Screenshot screenshot1;
  zx_status_t status = this->GetEnclosedGuest()->GetScenic()->CaptureScreenshot(&screenshot1);
  ASSERT_EQ(status, ZX_OK) << "Error capturing screenshot.";
  SaveScreenshot("input-state1", screenshot1);

  // Type a key, which should update the display.
  this->GetEnclosedGuest()->GetScenic()->SendKeyPress(KeyboardEventHidUsage::KEY_A);

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
