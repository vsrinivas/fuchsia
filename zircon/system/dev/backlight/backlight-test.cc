// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/backlight/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

#include <filesystem>
#include <vector>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace backlight {

namespace FidlBacklight = llcpp::fuchsia::hardware::backlight;

class BacklightDevice {
 public:
  BacklightDevice(zx::channel ch) : client_(FidlBacklight::Device::SyncClient(std::move(ch))) {
    if (GetBrightnessNormalized(&orig_brightness_) != ZX_OK) {
      printf("Error getting original brightness. Defaulting to 1.0\n");
      orig_brightness_ = 1.0;
    }
    printf("Brightness at the start of the test: %f\n", orig_brightness_);
  }

  ~BacklightDevice() {
    printf("Restoring original brightness...\n");
    if (SetBrightnessNormalized(orig_brightness_) != ZX_OK) {
      printf("Error setting brightness to %f\n", orig_brightness_);
    }
  }

  zx_status_t GetBrightnessNormalized(double* brightness) {
    auto response = client_.GetStateNormalized();
    zx_status_t status = response.ok()
                             ? (response->result.is_err() ? response->result.err() : ZX_OK)
                             : response.status();
    if (status != ZX_OK) {
      return status;
    }
    *brightness = response->result.response().state.brightness;
    return status;
  }

  zx_status_t SetBrightnessNormalized(double brightness) {
    FidlBacklight::State state = {.backlight_on = brightness > 0, .brightness = brightness};

    printf("Setting brightness to: %f\n", brightness);
    auto response = client_.SetStateNormalized(state);
    zx_status_t status = response.ok()
                             ? (response->result.is_err() ? response->result.err() : ZX_OK)
                             : response.status();
    return status;
  }

  zx_status_t GetBrightnessAbsolute(double* brightness) {
    auto response = client_.GetStateAbsolute();
    zx_status_t status = response.ok()
                             ? (response->result.is_err() ? response->result.err() : ZX_OK)
                             : response.status();
    if (status != ZX_OK) {
      return status;
    }
    *brightness = response->result.response().state.brightness;
    return status;
  }

  zx_status_t SetBrightnessAbsolute(double brightness) {
    FidlBacklight::State state = {.backlight_on = brightness > 0, .brightness = brightness};

    printf("Setting brightness to: %f nits\n", brightness);
    auto response = client_.SetStateAbsolute(state);
    zx_status_t status = response.ok()
                             ? (response->result.is_err() ? response->result.err() : ZX_OK)
                             : response.status();
    return status;
  }

 private:
  FidlBacklight::Device::SyncClient client_;
  double orig_brightness_;
};

class BacklightTest : public zxtest::Test {
 public:
  BacklightTest() {
    constexpr char kDevicePath[] = "/dev/class/backlight/";
    if (std::filesystem::exists(kDevicePath)) {
      for (const auto& entry : std::filesystem::directory_iterator(kDevicePath)) {
        printf("Found backlight device: %s\n", entry.path().c_str());
        fbl::unique_fd fd(open(entry.path().c_str(), O_RDONLY));
        EXPECT_GE(fd.get(), 0);

        // Open service handle.
        zx::channel channel;
        EXPECT_OK(fdio_get_service_handle(fd.release(), channel.reset_and_get_address()));
        devices_.push_back(std::make_unique<BacklightDevice>(std::move(channel)));
      }
    }
    if (devices_.empty()) {
      printf("No backlight devices found. Exiting...\n");
    }
  }

  double Approx(double val) { return round(val * 100.0) / 100.0; }

  void TestAllDevices() {
    for (auto& dev : devices_) {
      EXPECT_OK(dev->SetBrightnessNormalized(0));

      double brightness;
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 0);
      SleepIfDelayEnabled();

      EXPECT_OK(dev->SetBrightnessNormalized(0.25));
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 0.25);
      SleepIfDelayEnabled();

      EXPECT_OK(dev->SetBrightnessNormalized(0.5));
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 0.5);
      SleepIfDelayEnabled();

      EXPECT_OK(dev->SetBrightnessNormalized(0.75));
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 0.75);
      SleepIfDelayEnabled();

      EXPECT_OK(dev->SetBrightnessNormalized(1.0));
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 1.0);
      SleepIfDelayEnabled();

      EXPECT_OK(dev->SetBrightnessNormalized(0.75));
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 0.75);
      SleepIfDelayEnabled();

      EXPECT_OK(dev->SetBrightnessNormalized(0.5));
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 0.5);
      SleepIfDelayEnabled();

      EXPECT_OK(dev->SetBrightnessNormalized(0.25));
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 0.25);
      SleepIfDelayEnabled();

      EXPECT_OK(dev->SetBrightnessNormalized(0));
      EXPECT_OK(dev->GetBrightnessNormalized(&brightness));
      EXPECT_EQ(Approx(brightness), 0);
      SleepIfDelayEnabled();

      EXPECT_EQ(dev->SetBrightnessAbsolute(0), ZX_ERR_NOT_SUPPORTED);
      EXPECT_EQ(dev->GetBrightnessAbsolute(&brightness), ZX_ERR_NOT_SUPPORTED);
    }
  }

  static void RunWithDelays() { delayEnabled_ = true; }

  void SleepIfDelayEnabled() {
    if (delayEnabled_) {
      sleep(1);
    }
  }

 private:
  std::vector<std::unique_ptr<BacklightDevice>> devices_;
  static bool delayEnabled_;
};

TEST_F(BacklightTest, VaryBrightness) { TestAllDevices(); }

}  // namespace backlight

bool backlight::BacklightTest::delayEnabled_ = false;

int main(int argc, char** argv) {
  int opt;
  while ((opt = getopt(argc, argv, "dh")) != -1) {
    switch (opt) {
      case 'd':
        backlight::BacklightTest::RunWithDelays();
        argc--;
        break;
      case 'h':
      default:
        printf("Usage: runtests -t backlight-test [-- <options>]\n\n");
        printf(
            "  Valid options are:\n"
            "  -d : By default the test runs without any delays between brightness changes.\n"
            "       Pass the -d argument to space the brightness changes one second apart,\n"
            "       so that they are visually perceptible on the screen.\n"
            "  -h : Print this usage text.\n\n");
        return 0;
    }
  }

  return zxtest::RunAllTests(argc, argv);
}
