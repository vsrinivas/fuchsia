// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "garnet/bin/sshd-host/service.h"

const uint16_t kPort = 22;
const char* kKeyGenArgs[] = {"/pkg/bin/hostkeygen", nullptr};

int main(int /*argc*/, const char** /*argv*/) {
  // We need to close PA_DIRECTORY_REQUEST otherwise clients that expect us to
  // offer services won't know that we've started and are not going to offer
  // any services.
  //
  // TODO(abarth): Instead of closing this handle, we should offer some
  // introspection services for debugging.
  { zx::handle((zx_take_startup_handle(PA_DIRECTORY_REQUEST))); }

  auto service_directory = sys::ComponentContext::Create()->svc();
  // Ignore errors while provisioning authorized_keys.
  sshd_host::provision_authorized_keys_from_bootloader_file(service_directory);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_set_default_dispatcher(loop.dispatcher());

  zx::process process;
  fdio_spawn(0, FDIO_SPAWN_CLONE_ALL, kKeyGenArgs[0], kKeyGenArgs, process.reset_and_get_address());
  process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);

  sshd_host::Service service(kPort);

  loop.Run();
  async_set_default_dispatcher(nullptr);
  return 0;
}
