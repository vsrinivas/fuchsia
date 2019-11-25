// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/media/audio/examples/sync_sine/sync_sine.h"

namespace {

constexpr char kLowWaterMarkSwitch[] = "wake";
constexpr char kLowWaterMarkDefaultValue[] = "30";

constexpr char kHighWaterMarkSwitch[] = "sleep";
constexpr char kHighWaterMarkDefaultValue[] = "50";

constexpr char kFloatFormatSwitch[] = "float";
}  // namespace

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  // loop is needed by StartupContext.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  examples::MediaApp media_app(sys::ComponentContext::Create());

  if (command_line.HasOption("v") || command_line.HasOption("verbose")) {
    media_app.set_verbose(true);
  }

  std::string low_water_mark_ms =
      command_line.GetOptionValueWithDefault(kLowWaterMarkSwitch, kLowWaterMarkDefaultValue);
  media_app.set_low_water_mark_from_ms(fxl::StringToNumber<int64_t>(low_water_mark_ms));

  std::string high_water_mark_ms =
      command_line.GetOptionValueWithDefault(kHighWaterMarkSwitch, kHighWaterMarkDefaultValue);
  media_app.set_high_water_mark_from_ms(fxl::StringToNumber<int64_t>(high_water_mark_ms));

  if (command_line.HasOption(kFloatFormatSwitch)) {
    media_app.set_float(true);
  }

  return media_app.Run();
}
