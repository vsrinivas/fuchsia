// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>

#include <presentation/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "garnet/bin/ui/root_presenter/renderer_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  FXL_LOG(ERROR) << "This tool is intended for testing and debugging purposes "
                    "only and may cause problems if invoked incorrectly.";

  root_presenter::RendererParams presenter_renderer_params =
      root_presenter::RendererParams::FromCommandLine(command_line);

  bool clipping_enabled = true;
  if (presenter_renderer_params.clipping_enabled.has_value()) {
    clipping_enabled = presenter_renderer_params.clipping_enabled.value();
  }

  fidl::VectorPtr<fuchsia::ui::gfx::RendererParam> renderer_params;
  if (presenter_renderer_params.render_frequency.has_value()) {
    fuchsia::ui::gfx::RendererParam param;
    param.set_render_frequency(
        presenter_renderer_params.render_frequency.value());
    renderer_params.push_back(std::move(param));
  }
  if (presenter_renderer_params.shadow_technique.has_value()) {
    fuchsia::ui::gfx::RendererParam param;
    param.set_shadow_technique(
        presenter_renderer_params.shadow_technique.value());
    renderer_params.push_back(std::move(param));
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto application_context_ =
      component::ApplicationContext::CreateFromStartupInfo();

  // Ask the presenter to change renderer params.
  auto presenter = application_context_
                       ->ConnectToEnvironmentService<presentation::Presenter>();
  presenter->HACK_SetRendererParams(clipping_enabled,
                                    std::move(renderer_params));

  // Done!
  async::PostTask(loop.async(), [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
