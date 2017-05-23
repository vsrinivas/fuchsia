// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/syscalls/object.h>

#include "application/lib/app/application_context.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

void ReportAndTeardown(test_runner::TestRunner* test_runner,
                       int test_result,
                       const char* error) {
  if (test_result != 0) {
    test_runner->Fail(error);
  }
  test_runner->Teardown([] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  mtl::MessageLoop::GetCurrent()->Run();
}

// Runs a command specified by argv, and based on its exit code reports success
// or failure to the TestRunner FIDL service.
int main(int argc, char** argv) {
  char* executable = argv[1];

  mtl::MessageLoop message_loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  auto test_runner =
      app_context->ConnectToEnvironmentService<test_runner::TestRunner>();
  test_runner->Identify(executable);

  app::ApplicationEnvironmentPtr environment;
  app_context->environment()->Duplicate(environment.NewRequest());
  launchpad_t* launchpad;
  launchpad_create(0, executable, &launchpad);
  launchpad_load_from_file(launchpad, executable);
  launchpad_clone(launchpad, LP_CLONE_ALL);
  launchpad_set_args(launchpad, argc - 1, argv + 1);

  const char* error;
  mx_handle_t handle;
  mx_status_t status = launchpad_go(launchpad, &handle, &error);
  if (status < 0) {
    ReportAndTeardown(test_runner.get(), 1, error);
    return 1;
  }

  status =
      mx_object_wait_one(handle, MX_PROCESS_SIGNALED, MX_TIME_INFINITE, NULL);
  if (status != NO_ERROR) {
    ReportAndTeardown(test_runner.get(), 1, "Failed to wait for exit");
    return 1;
  }

  mx_info_process_t proc_info;
  status = mx_object_get_info(handle, MX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), NULL, NULL);
  mx_handle_close(handle);
  if (status < 0) {
    ReportAndTeardown(test_runner.get(), 1, "Failed to get return code");
    return 1;
  }

  ReportAndTeardown(test_runner.get(), proc_info.return_code,
                    "Non-zero return code");
  return 0;
}
