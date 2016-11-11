// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/trace/trace_app.h"
#include "lib/ftl/log_settings.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  auto configuration = tracing::Configuration::ParseOrExit(command_line);

  mtl::MessageLoop loop;
  tracing::TraceApp trace_app(std::move(configuration));
  loop.Run();
  return 0;
}
