// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "light_stress.h"

#include <fuchsia/hardware/light/cpp/fidl.h>
#include <lib/zx/clock.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include "args.h"
#include "device.h"
#include "status.h"
#include "util.h"

namespace hwstress {

constexpr std::string_view kDefaultLightDevicePath = "/dev/class/light/000";

namespace {

zx::status<> LightErrorToZxStatus(fuchsia::hardware::light::LightError error) {
  switch (error) {
    case fuchsia::hardware::light::LightError::OK:
      return zx::error(ZX_OK);
    case fuchsia::hardware::light::LightError::INVALID_INDEX:
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    case fuchsia::hardware::light::LightError::NOT_SUPPORTED:
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    case fuchsia::hardware::light::LightError::FAILED:
      return zx::error(ZX_ERR_IO);
    default:
      return zx::error(ZX_ERR_INTERNAL);
  }
}

zx::status<> SetLightBrightness(const fuchsia::hardware::light::LightSyncPtr& light,
                                uint32_t light_num, double brightness) {
  fuchsia::hardware::light::Light_SetBrightnessValue_Result result;
  light->SetBrightnessValue(light_num, brightness, &result);
  if (result.is_err()) {
    return LightErrorToZxStatus(result.err());
  }
  return zx::ok();
}

}  // namespace

bool operator==(const LightInfo& a, const LightInfo& b) {
  return std::tie(a.name, b.index) == std::tie(b.name, b.index);
}
bool operator!=(const LightInfo& a, const LightInfo& b) { return !(a == b); }

zx::status<> TurnOnLight(const fuchsia::hardware::light::LightSyncPtr& light, uint32_t light_num) {
  return SetLightBrightness(light, light_num, /*brightness=*/1.0);
}

zx::status<> TurnOffLight(const fuchsia::hardware::light::LightSyncPtr& light, uint32_t light_num) {
  return SetLightBrightness(light, light_num, /*brightness=*/0.0);
}

zx::status<std::vector<LightInfo>> GetLights(const fuchsia::hardware::light::LightSyncPtr& light) {
  // Get the number of lights.
  uint32_t num_lights;
  zx_status_t status = light->GetNumLights(&num_lights);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Read information about the lights.
  std::vector<LightInfo> result;
  result.reserve(num_lights);
  for (uint32_t i = 0; i < num_lights; i++) {
    // Fetch information about the i'th light.
    fuchsia::hardware::light::Light_GetInfo_Result info;
    zx_status_t status = light->GetInfo(i, &info);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    if (info.is_err()) {
      return LightErrorToZxStatus(info.err()).take_error();
    }

    // Ignore unsupported lights.
    if (info.response().info.capability != fuchsia::hardware::light::Capability::BRIGHTNESS) {
      fprintf(stderr, "Light %d '%s' is unsupported.\n", i, info.response().info.name.c_str());
      continue;
    }

    // Convert to a result.
    result.push_back(LightInfo{
        .name = info.response().info.name,
        .index = i,
    });
  }

  return zx::ok(result);
}

bool StressLight(StatusLine* status, const CommandLineArgs& args, zx::duration duration) {
  // Open the light device.
  zx::status<zx::channel> channel = OpenDeviceChannel(kDefaultLightDevicePath);
  if (channel.is_error()) {
    status->Log("Could not open device: %s\n", channel.status_string());
    return false;
  }
  fuchsia::hardware::light::LightSyncPtr light_dev{};
  light_dev.Bind(std::move(channel).value());
  // Fetch information about the lights.
  zx::status<std::vector<LightInfo>> lights_or = GetLights(light_dev);
  if (lights_or.is_error()) {
    status->Log("Could not query lights: %s\n", lights_or.status_string());
    return false;
  }
  std::vector<LightInfo> lights = std::move(lights_or).value();

  // If there are no lights, abort.
  if (lights.empty()) {
    status->Log("No supported lights found.");
    return false;
  }

  // Print out information about lights.
  status->Log("Found %ld light(s):", lights.size());
  for (const LightInfo& light : lights) {
    status->Log("  %s (%d)", light.name.c_str(), light.index);
  }

  // Turn lights on and off until time runs out.
  zx::time start_time = zx::clock::get_monotonic();
  zx::time end_time = start_time + duration;
  while (zx::clock::get_monotonic() < end_time) {
    // Turn lights on.
    for (const LightInfo& light : lights) {
      zx::status<> result = TurnOnLight(light_dev, light.index);
      if (result.is_error()) {
        status->Log("Could not turn on light %d '%s': %s", light.index, light.name.c_str(),
                    result.status_string());
      }
    }

    zx::nanosleep(zx::deadline_after(SecsToDuration(args.light_on_time_seconds)));

    // Turn all lights off.
    for (const LightInfo& light : lights) {
      zx::status<> result = TurnOffLight(light_dev, light.index);
      if (result.is_error()) {
        status->Log("Could not turn off light %d '%s': %s", light.index, light.name.c_str(),
                    result.status_string());
      }
    }

    zx::nanosleep(zx::deadline_after(SecsToDuration(args.light_off_time_seconds)));
  }

  return true;
}

}  // namespace hwstress
