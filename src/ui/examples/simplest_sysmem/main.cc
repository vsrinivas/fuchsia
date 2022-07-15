// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/examples/simplest_sysmem/view_provider.h"

int main(int argc, const char* argv[], char* envp[]) {
  sysmem_example::RenderType type;

  // NOTE: Currently `ffx session add` doesn't support argument passing for .cm.
  // Until its supported, you'll have to change the arg in the .cml file.
  //
  // TODO(fxbug.dev/96004): Update instruction once `ffx session add` supports argument passing.
  if (argc == 2) {
    if (!strcmp(argv[1], "--png")) {
      type = sysmem_example::RenderType::PNG;
    } else if (!strcmp(argv[1], "--rect")) {
      type = sysmem_example::RenderType::RECTANGLE;
    } else if (!strcmp(argv[1], "--block")) {
      type = sysmem_example::RenderType::COLOR_BLOCK;
    } else {
      FX_LOGS(ERROR) << "invalid argument: " << argv[1]
                     << ". Please specify one of --png, --rect, --block\n";
    }
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  // NOTE: To avoid race-condition , we should call to create & serve context
  // separately.
  auto context = sys::ComponentContext::Create();
  auto simplest_sysmem_view =
      std::make_unique<sysmem_example::ViewProviderImpl>(context.get(), type);
  context->outgoing()->ServeFromStartupInfo();
  loop.Run();
  return 0;
}
