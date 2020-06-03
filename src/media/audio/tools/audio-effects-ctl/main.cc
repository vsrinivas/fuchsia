// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/audio/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/fxl/command_line.h"

void PrintUsage(const fxl::CommandLine& options);
int HandleUpdateEffectCommand(const fxl::CommandLine& options);

int main(int argc, const char** argv) {
  auto options = fxl::CommandLineFromArgcArgv(argc, argv);
  if (options.positional_args().size() < 1) {
    PrintUsage(options);
    return -1;
  }
  const auto& command = options.positional_args()[0];
  if (command == "update") {
    return HandleUpdateEffectCommand(options);
  }

  PrintUsage(options);
  return -1;
}

void PrintUsage(const fxl::CommandLine& options) {
  fprintf(stderr, "Usage: %s update EFFECT_NAME MESSAGE\n", options.argv0().c_str());
  fprintf(stderr, "\n");
  fprintf(stderr, "This is a simple CLI proxy for EffectsController. See the FIDL documentation\n");
  fprintf(stderr, "for more details:\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "https://fuchsia.dev/reference/fidl/fuchsia.media.audio#EffectsController\n");
}

const char* UpdateEffectErrorToString(const fuchsia::media::audio::UpdateEffectError& error) {
  switch (error) {
    case fuchsia::media::audio::UpdateEffectError::NOT_FOUND:
      return "NOT_FOUND";
    case fuchsia::media::audio::UpdateEffectError::INVALID_CONFIG:
      return "INVALID_CONFIG";
    default:
      return "(unknown)";
  }
}

int HandleUpdateEffectCommand(const fxl::CommandLine& options) {
  if (options.positional_args().size() != 3) {
    PrintUsage(options);
    return -1;
  }
  const auto& effect_name = options.positional_args()[1];
  const auto& effect_update = options.positional_args()[2];

  auto svc = sys::ServiceDirectory::CreateFromNamespace();
  fuchsia::media::audio::EffectsControllerSyncPtr effects_controller;
  svc->Connect(effects_controller.NewRequest());

  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status = effects_controller->UpdateEffect(effect_name, effect_update, &result);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to send UpdateEffect FIDL command: %s\n", zx_status_get_string(status));
    return -2;
  }
  if (result.is_err()) {
    fprintf(stderr, "EffectsController.UpdateEffect failed: %s\n",
            UpdateEffectErrorToString(result.err()));
    return -3;
  }
  return 0;
}
