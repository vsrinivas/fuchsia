// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <unistd.h>

#include "src/recovery/factory_reset/factory_reset.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fbl::unique_fd dev_fd;
  dev_fd.reset(open("/dev", O_RDONLY | O_DIRECTORY));

  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::Create();

  auto admin =
      context->svc()->Connect<fuchsia::device::manager::Administrator>();
  factory_reset::FactoryReset factory_reset(std::move(dev_fd),
                                            std::move(admin));
  fidl::BindingSet<fuchsia::recovery::FactoryReset> bindings;
  context->outgoing()->AddPublicService(bindings.GetHandler(&factory_reset));
  loop.Run();
  return 0;
}
