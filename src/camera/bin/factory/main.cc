// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/camera/bin/factory/factory_server.h"

int main(int argc, char* argv[]) {
  // Create the factory server.
  auto factory_server_result = camera::FactoryServer::Create();
  if (factory_server_result.is_error()) {
    FX_PLOGS(ERROR, factory_server_result.error()) << "Failed to create FactoryServer.";
    return EXIT_FAILURE;
  }
  auto factory_server = factory_server_result.take_value();

  return EXIT_SUCCESS;
}
