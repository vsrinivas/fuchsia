// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/intl/intl_property_provider_impl/intl_property_provider_impl.h"

using intl::IntlPropertyProviderImpl;

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    exit(EXIT_FAILURE);
  }
  syslog::SetTags({"intl_services"});
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  FX_LOGS(INFO) << "Started.";

  std::unique_ptr<IntlPropertyProviderImpl> intl = IntlPropertyProviderImpl::Create(context->svc());

  context->outgoing()->AddPublicService(intl->GetHandler());

  const zx_status_t status = loop.Run();

  FX_LOGS(INFO) << "Terminated with status: " << zx_status_get_string(status);
  exit(status);
}
