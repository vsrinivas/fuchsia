// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/fdio/fdio.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/io/device_watcher.h"

namespace fir = fuchsia_input_report;

struct Midpoints {
  int64_t x_midpoint;
  int64_t y_midpoint;
};

enum class Quadrant { kTopLeft, kTopRight, kBottomLeft, kBottomRight };

void ConfigureTouchEvents(fidl::WireSyncClient<fir::InputDevice>& client);
void ConnectToTouchpad(fidl::WireSyncClient<fir::InputDevice>* out_client,
                       Midpoints* out_midpoints);
void WaitForTouchAndRelease(fidl::WireSyncClient<fir::InputReportsReader>& client,
                            Midpoints midpoints, Quadrant desired_quadrant);

TEST(TouchpadTests, AreaCoverage) {
  // This test verifies that the touchpad driver can report touches at all four corners of the
  // touchpad.

  fidl::WireSyncClient<fir::InputDevice> input_device_client;
  Midpoints midpoints;
  ConnectToTouchpad(&input_device_client, &midpoints);
  ASSERT_TRUE(input_device_client.is_valid());
  ConfigureTouchEvents(input_device_client);

  // Get the InputReportsReader client from the InputDevice protocol.
  auto endpoints = fidl::CreateEndpoints<fir::InputReportsReader>();
  ASSERT_TRUE(endpoints.is_ok());
  ASSERT_EQ(ZX_OK,
            input_device_client->GetInputReportsReader(std::move(endpoints->server)).status());

  auto reader_client = fidl::WireSyncClient<fir::InputReportsReader>(std::move(endpoints->client));

  // The test itself - check for touches in each corner.
  WaitForTouchAndRelease(reader_client, midpoints, Quadrant::kTopLeft);
  WaitForTouchAndRelease(reader_client, midpoints, Quadrant::kTopRight);
  WaitForTouchAndRelease(reader_client, midpoints, Quadrant::kBottomRight);
  WaitForTouchAndRelease(reader_client, midpoints, Quadrant::kBottomLeft);
}

Quadrant GetQuadrant(const fir::wire::ContactInputReport& contact, Midpoints midpoints) {
  bool in_left_half = contact.position_x() < midpoints.x_midpoint;
  bool in_top_half = contact.position_y() < midpoints.y_midpoint;

  if (in_left_half) {
    return in_top_half ? Quadrant::kTopLeft : Quadrant::kBottomLeft;
  } else {
    return in_top_half ? Quadrant::kTopRight : Quadrant::kBottomRight;
  }
}

const char* GetQuadrantName(Quadrant q) {
  switch (q) {
    case Quadrant::kTopLeft:
      return "top left";
    case Quadrant::kTopRight:
      return "top right";
    case Quadrant::kBottomLeft:
      return "bottom left";
    case Quadrant::kBottomRight:
      return "bottom right";
  }
}

void WaitForRelease(fidl::WireSyncClient<fir::InputReportsReader>& client) {
  // Wait for the touch to be released (indicated by an empty contacts vector).
  while (true) {
    auto result = client->ReadInputReports();
    ASSERT_EQ(ZX_OK, result.status());
    for (fir::wire::InputReport& report : result->value()->reports) {
      if (report.has_touch() && report.touch().contacts().empty()) {
        fputs("Release detected.\n", stderr);
        return;
      }
    }
  }
}

void WaitForTouch(fidl::WireSyncClient<fir::InputReportsReader>& client, Midpoints midpoints,
                  Quadrant desired_quadrant) {
  // warned_quadrant stores a value if the manual tester touches the wrong quadrant, so we don't
  // bombard them with error messages.
  std::optional<Quadrant> warned_quadrant;
  while (true) {
    auto result = client->ReadInputReports();
    ASSERT_EQ(ZX_OK, result.status());

    // If the reports is empty then we definitely haven't received a touch.
    if (result->value()->reports.empty()) {
      continue;
    }

    // Wait for a touch event. We ensure that all reports in the FIDL response contain valid touch
    // reports in the expected quadrant.
    bool got_touch = true;
    for (fir::wire::InputReport& report : result->value()->reports) {
      if (report.has_touch() && !report.touch().contacts().empty()) {
        const fir::wire::ContactInputReport& contact_report = report.touch().contacts()[0];
        Quadrant quadrant = GetQuadrant(contact_report, midpoints);
        if (quadrant != desired_quadrant) {
          if (warned_quadrant != quadrant) {
            fprintf(stderr, "Touch detected in the %s. Please touch the %s corner instead.\n",
                    GetQuadrantName(quadrant), GetQuadrantName(desired_quadrant));
          }
          warned_quadrant = quadrant;
          got_touch = false;
        }
      } else {
        // Report doesn't have touch data or has no contacts.
        got_touch = false;
      }
    }

    if (got_touch) {
      fputs("Touch detected. Please release finger.\n", stderr);
      break;
    }
  }
}

void WaitForTouchAndRelease(fidl::WireSyncClient<fir::InputReportsReader>& client,
                            Midpoints midpoints, Quadrant desired_quadrant) {
  fprintf(stderr, "\n\n*** Please touch the %s corner of the touchpad and hold\n",
          GetQuadrantName(desired_quadrant));

  WaitForTouch(client, midpoints, desired_quadrant);
  WaitForRelease(client);
}

void ConnectToTouchpad(fidl::WireSyncClient<fir::InputDevice>* out_client,
                       Midpoints* out_midpoints) {
  // Output parameters are used because ASSERT_* macros only work in functions that return void.

  // Iterate over the devices in /dev/class/input-report/ looking for the one that corresponds to
  // the touchpad.
  const char* devfs_path = "/dev/class/input-report/";
  DIR* dir = opendir(devfs_path);
  ASSERT_NE(dir, nullptr);

  struct dirent* de;
  while ((de = readdir(dir)) != nullptr) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      continue;
    }

    // Open the /dev/class entry as an InputDevice FIDL client.
    fbl::unique_fd fd(openat(dirfd(dir), de->d_name, O_RDONLY));
    ASSERT_TRUE(fd.is_valid());

    zx::channel chan;
    zx_status_t status = fdio_get_service_handle(fd.release(), chan.reset_and_get_address());
    ASSERT_EQ(ZX_OK, status);

    auto input_device_client =
        fidl::WireSyncClient(fidl::ClientEnd<fir::InputDevice>(std::move(chan)));

    // Get the device's descriptor and skip devices that aren't touchpads.
    auto descriptor_result = input_device_client->GetDescriptor();
    ASSERT_EQ(ZX_OK, descriptor_result.status());
    if (!descriptor_result->descriptor.has_touch() ||
        descriptor_result->descriptor.touch().input().touch_type() != fir::TouchType::kTouchpad) {
      continue;
    }

    // Need at least one contact entry to get the dimensions of the touchpad.
    ASSERT_FALSE(descriptor_result->descriptor.touch().input().contacts().empty());
    const fir::wire::ContactInputDescriptor& contact =
        descriptor_result->descriptor.touch().input().contacts()[0];
    int64_t min_x = contact.position_x().range.min;
    int64_t max_x = contact.position_x().range.max;
    int64_t min_y = contact.position_y().range.min;
    int64_t max_y = contact.position_y().range.max;
    Midpoints midpoints{
        .x_midpoint = (max_x + min_x) / 2,
        .y_midpoint = (max_y + min_y) / 2,
    };

    *out_client = std::move(input_device_client);
    *out_midpoints = midpoints;
    return;
  }

  FAIL() << "no touchpad device found under " << devfs_path;
}

void ConfigureTouchEvents(fidl::WireSyncClient<fir::InputDevice>& client) {
  // By default the touchpad only reports mouse events, so we use SetFeatureReport to turn on
  // touch events, which allow us to detect when a finger is released.
  fidl::Arena allocator;
  auto touch_report = fir::wire::TouchFeatureReport::Builder(allocator);
  touch_report.input_mode(fir::TouchConfigurationInputMode::kWindowsPrecisionTouchpadCollection);
  auto feature_report = fir::wire::FeatureReport::Builder(allocator);
  feature_report.touch(touch_report.Build());
  ASSERT_EQ(ZX_OK, client->SetFeatureReport(feature_report.Build()).status());
}
