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
  arguments["zircon.autorun.system"] = "ls+/system-delayed";

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  std::optional<console_launcher::Arguments> args = console_launcher::GetArguments(&boot_args);
  ASSERT_TRUE(args.has_value());

  ASSERT_TRUE(args->run_shell);
  ASSERT_TRUE(args->is_virtio);
  ASSERT_EQ(args->term.compare("TERM=FAKE_TERM"), 0);
  ASSERT_EQ(args->device.compare("/test/path"), 0);
  ASSERT_EQ(args->autorun_boot.compare("ls+/dev/class/"), 0);
  ASSERT_EQ(args->autorun_system.compare("ls+/system-delayed"), 0);
}

TEST(SystemInstanceTest, CheckBootArgDefaultStrings) {
  std::map<std::string, std::string> arguments;
  arguments["kernel.shell"] = "true";
  arguments["console.shell"] = "true";
  arguments["console.is_virtio"] = "false";

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  std::optional<console_launcher::Arguments> args = console_launcher::GetArguments(&boot_args);
  ASSERT_TRUE(args.has_value());

  ASSERT_FALSE(args->run_shell);
  ASSERT_FALSE(args->is_virtio);
  ASSERT_EQ(args->term.compare("TERM=uart"), 0);
  ASSERT_EQ(args->device.compare("/svc/console"), 0);
  ASSERT_EQ(args->autorun_boot.compare(""), 0);
  ASSERT_EQ(args->autorun_system.compare(""), 0);
}
