// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "fs/pseudo-dir.h"
#include "fs/service.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/component/cpp/outgoing.h"
#include "lib/component/cpp/testing/fake_launcher.h"
#include "lib/fdio/spawn.h"
#include "lib/fidl/cpp/binding_set.h"

TEST(Run, Daemonize) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  const char* run_d_command_argv[] = {"/system/bin/run", "-d",
                                      "test_program_name", nullptr};

  zx::job job;
  uint32_t flags =
      FDIO_SPAWN_CLONE_LDSVC | FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_CLONE_STDIO;

  int launcher_create_calls = 0;
  fuchsia::sys::LaunchInfo received_launch_info;
  fidl::InterfaceRequest<fuchsia::sys::ComponentController> received_controller;

  component::testing::FakeLauncher test_launcher;
  test_launcher.RegisterComponent(
      "test_program_name",
      [&launcher_create_calls, &received_launch_info, &received_controller](
          fuchsia::sys::LaunchInfo info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController>
              controller) {
        launcher_create_calls++;
        received_launch_info = std::move(info);
        received_controller = std::move(controller);
      });

  component::Outgoing outgoing_services;

  fs::SynchronousVfs vfs(loop.dispatcher());

  fidl::BindingSet<fuchsia::sys::Launcher> launcher_bindings;
  outgoing_services.AddPublicService(
      launcher_bindings.GetHandler(&test_launcher));

  zx::channel svc_req, svc_dir;
  ASSERT_EQ(ZX_OK, zx::channel::create(0u, &svc_req, &svc_dir));

  auto public_dir = outgoing_services.public_dir();
  ASSERT_EQ(ZX_OK, vfs.ServeDirectory(public_dir, std::move(svc_req)));

  std::vector<fdio_spawn_action_t> actions{
      {.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
       .ns = {
           "/svc",
           svc_dir.release(),
       }}};

  zx::process run_process;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];

  ASSERT_EQ(ZX_OK, fdio_spawn_etc(job.get(), flags,
                                  run_d_command_argv[0],  // path
                                  run_d_command_argv,
                                  nullptr,  // environ
                                  actions.size(), actions.data(),
                                  run_process.reset_and_get_address(), err_msg))
      << err_msg;

  // Wait for the "run" program to exit.
  EXPECT_EQ(ZX_OK, zx_object_wait_one(run_process.get(), ZX_TASK_TERMINATED,
                                      ZX_TIME_INFINITE, nullptr));

  // Check that it succeeded.
  zx_info_process_t proc_info;
  EXPECT_EQ(ZX_OK,
            zx_object_get_info(run_process.get(), ZX_INFO_PROCESS, &proc_info,
                               sizeof(proc_info), nullptr, nullptr));

  EXPECT_EQ(0, proc_info.return_code);

  // Spin our loop to receive any message the "run" program sent to the launcher
  // service from its environment.
  loop.RunUntilIdle();

  // We should get one launch call with launch_info corresponding to our command
  // line argument and a null controller.
  EXPECT_EQ(1, launcher_create_calls);

  EXPECT_EQ("test_program_name", received_launch_info.url);

  EXPECT_FALSE(received_controller.is_valid());
}
