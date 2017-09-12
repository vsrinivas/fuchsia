// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/examples/swap_cpp/module.h"
#include "lib/fsl/tasks/message_loop.h"

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  modular_example::ModuleApp app(
      [](auto view_manager, auto view_owner_request) {
        return new modular_example::ModuleView(
            std::move(view_manager), std::move(view_owner_request), 0xFF00FFFF);
      });
  loop.Run();
  return 0;
}
