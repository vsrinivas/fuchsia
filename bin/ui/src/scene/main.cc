// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/mozart/src/scene/composer_app.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  using namespace mozart;
  using namespace mozart::composer;

  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  ComposerApp::Params params;
  if (!params.Setup(command_line))
    return 1;

  mtl::MessageLoop loop;
  ComposerApp app(&params);

  loop.Run();
  return 0;
}
