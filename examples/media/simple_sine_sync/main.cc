// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/fxl/command_line.h"
#include "lib/fxl/strings/string_number_conversions.h"

#include "garnet/examples/media/simple_sine_sync/simple_sine_sync.h"

namespace {

constexpr char kFirstPtsDelaySwitch[] = "lead";
constexpr char kFirstPtsDelayDefaultValue[] = "5";

constexpr char kLowWaterMarkSwitch[] = "wake";
constexpr char kLowWaterMarkDefaultValue[] = "30";

constexpr char kHighWaterMarkSwitch[] = "sleep";
constexpr char kHighWaterMarkDefaultValue[] = "50";
}  // namespace

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  examples::MediaApp media_app;

  if (command_line.HasOption("v") || command_line.HasOption("verbose"))
    media_app.set_verbose(true);

  std::string first_pts_delay = command_line.GetOptionValueWithDefault(
      kFirstPtsDelaySwitch, kFirstPtsDelayDefaultValue);
  media_app.set_first_pts_delay_ms(
      fxl::StringToNumber<int64_t>(first_pts_delay));

  std::string low_water_mark_ms = command_line.GetOptionValueWithDefault(
      kLowWaterMarkSwitch, kLowWaterMarkDefaultValue);
  media_app.set_low_water_mark_ms(
      fxl::StringToNumber<int64_t>(low_water_mark_ms));

  std::string high_water_mark_ms = command_line.GetOptionValueWithDefault(
      kHighWaterMarkSwitch, kHighWaterMarkDefaultValue);
  media_app.set_high_water_mark_ms(
      fxl::StringToNumber<int64_t>(high_water_mark_ms));

  return media_app.Run();
}
