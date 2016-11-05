// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/mozart/src/compositor/compositor_app.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  std::unique_ptr<compositor::CompositorApp> app;
  loop.task_runner()->PostTask(
      [&app] { app = std::make_unique<compositor::CompositorApp>(); });

  loop.Run();
  return 0;
}
