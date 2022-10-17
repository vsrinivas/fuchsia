// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/console-launcher/console_launcher.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

TEST(SystemInstanceTest, CheckBootArgParsing) {
  std::map<std::string, std::string> arguments;
  arguments["kernel.shell"] = "false";
  arguments["console.shell"] = "true";
  arguments["console.is_virtio"] = "true";
  arguments["console.path"] = "/test/path";
  arguments["TERM"] = "FAKE_TERM";
  arguments["zircon.autorun.boot"] = "ls+/dev/class/";
  arguments["zircon.autorun.system"] = "ls+/system";

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  zx::result args = console_launcher::GetArguments(boot_args.client_end());
  ASSERT_OK(args.status_value());

  ASSERT_TRUE(args->run_shell);
  ASSERT_TRUE(args->device.is_virtio);
  ASSERT_EQ(args->term, "TERM=FAKE_TERM");
  ASSERT_EQ(args->device.path, "/test/path");
  ASSERT_EQ(args->autorun_boot, "ls+/dev/class/");
  ASSERT_EQ(args->autorun_system, "ls+/system");
}

TEST(SystemInstanceTest, CheckBootArgDefaultStrings) {
  std::map<std::string, std::string> arguments;
  arguments["kernel.shell"] = "true";
  arguments["console.shell"] = "true";
  arguments["console.is_virtio"] = "false";

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  zx::result args = console_launcher::GetArguments(boot_args.client_end());
  ASSERT_OK(args.status_value());

  ASSERT_FALSE(args->run_shell);
  ASSERT_FALSE(args->device.is_virtio);
  ASSERT_EQ(args->term, "TERM=uart");
  ASSERT_EQ(args->device.path, "/svc/console");
  ASSERT_EQ(args->autorun_boot, "");
  ASSERT_EQ(args->autorun_system, "");
}

// The defaults are that a system is not required, so zedboot will try to launch.
TEST(VirtconSetup, VirtconDefaults) {
  std::map<std::string, std::string> arguments;

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  zx::result args = console_launcher::GetArguments(boot_args.client_end());
  ASSERT_OK(args.status_value());

  ASSERT_FALSE(args->virtual_console_need_debuglog);
}

// Need debuglog should be true when netboot is true and netboot is not disabled.
TEST(VirtconSetup, VirtconNeedDebuglog) {
  std::map<std::string, std::string> arguments;
  arguments["netsvc.disable"] = "false";
  arguments["netsvc.netboot"] = "true";

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  zx::result args = console_launcher::GetArguments(boot_args.client_end());
  ASSERT_OK(args.status_value());

  ASSERT_TRUE(args->virtual_console_need_debuglog);
}

// If netboot is true but netsvc is disabled, don't start debuglog.
TEST(VirtconSetup, VirtconNetbootWithNetsvcDisabled) {
  std::map<std::string, std::string> arguments;
  arguments["netsvc.disable"] = "true";
  arguments["netsvc.netboot"] = "true";

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  zx::result args = console_launcher::GetArguments(boot_args.client_end());
  ASSERT_OK(args.status_value());

  ASSERT_FALSE(args->virtual_console_need_debuglog);
}
