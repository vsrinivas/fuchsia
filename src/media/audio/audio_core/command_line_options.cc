// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/command_line_options.h"

#include "src/lib/fxl/arraysize.h"

namespace media::audio {
namespace {
struct Arg {
  const char* name;
  const char* help;
};

static constexpr const char* kDisableDeviceSettingsWriteArg = "disable-device-settings-writeback";

static Arg kSupportedArgs[] = {
    {kDisableDeviceSettingsWriteArg,
     "Prevents device settings from being written back to persistent storage"},
};

void PrintSupportedArguments() {
  FXL_LOG(ERROR) << "Supported audio_core arguments:";
  for (size_t i = 0; i < arraysize(kSupportedArgs); ++i) {
    FXL_LOG(ERROR) << "  --" << kSupportedArgs[i].name << ": " << kSupportedArgs[i].help;
  }
}

}  // namespace

// static
fit::result<CommandLineOptions, zx_status_t> CommandLineOptions::ParseFromArgcArgv(
    int argc, const char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  CommandLineOptions result;

  if (cl.positional_args().size() > 0) {
    FXL_LOG(ERROR) << "Received unsupported positional args:";
    for (const auto& arg : cl.positional_args()) {
      FXL_LOG(ERROR) << "    " << arg;
    }
    PrintSupportedArguments();
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  for (const fxl::CommandLine::Option& option : cl.options()) {
    if (option.name == kDisableDeviceSettingsWriteArg) {
      if (!option.value.empty()) {
        FXL_LOG(ERROR) << "--" << kDisableDeviceSettingsWriteArg << " should not have a value";
        PrintSupportedArguments();
        return fit::error(ZX_ERR_INVALID_ARGS);
      }
      result.enable_device_settings_writeback = false;
    } else {
      FXL_LOG(ERROR) << "Unknown option '" << option.name << "'";
      PrintSupportedArguments();
      return fit::error(ZX_ERR_INVALID_ARGS);
    }
  }
  return fit::ok(result);
}

}  // namespace media::audio
