// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <magenta/syscalls/object.h>

#include "application/lib/app/application_context.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, char** argv) {
  char* executable = argv[1];

  mtl::MessageLoop message_loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  auto test_runner =
      app_context->ConnectToEnvironmentService<test_runner::TestRunner>();
  test_runner->Identify(executable);

  launchpad_t* launchpad;
  launchpad_create(0, executable, &launchpad);
  launchpad_load_from_file(launchpad, executable);
  launchpad_clone(launchpad, LP_CLONE_ALL);
  launchpad_set_args(launchpad, argc - 1, argv + 1);

  const char* error;
  mx_handle_t handle;
  mx_status_t status = launchpad_go(launchpad, &handle, &error);
  if (status < 0) {
    test_runner->Fail(error);
    test_runner->Teardown();
    return 1;
  }

  status = mx_object_wait_one(
      handle, MX_PROCESS_SIGNALED, MX_TIME_INFINITE, NULL);
  if (status != NO_ERROR) {
    test_runner->Fail("Failed to wait for exit");
    test_runner->Teardown();
    return 1;
  }

  mx_info_process_t proc_info;
  status = mx_object_get_info(
      handle, MX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL);
  mx_handle_close(handle);
  if (status < 0) {
    test_runner->Fail("Failed to get return code");
    test_runner->Teardown();
    return 1;
  }

  if (proc_info.return_code != 0) {
    test_runner->Fail("Non-zero return code");
  }

  test_runner->Teardown();
  return 0;
}
