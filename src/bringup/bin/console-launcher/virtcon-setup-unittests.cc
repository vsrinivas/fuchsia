// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include <map>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

#include "src/bringup/bin/console-launcher/virtcon-setup.h"

// The defaults are that a system is not required, so zedboot will try to launch.
TEST(VirtconSetup, VirtconDefaults) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  std::map<std::string, std::string> arguments;
  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  auto result = console_launcher::GetVirtconArgs(&boot_args);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().should_launch);
  ASSERT_FALSE(result.value().need_debuglog);
}

// Need debuglog should be true when netboot is true and netboot is not disabled.
TEST(VirtconSetup, VirtconNeedDebuglog) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  std::map<std::string, std::string> arguments;
  arguments["netsvc.disable"] = "false";
  arguments["netsvc.netboot"] = "true";

  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  auto result = console_launcher::GetVirtconArgs(&boot_args);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().need_debuglog);
}

// If netboot is true but netsvc is disabled, don't start debuglog.
TEST(VirtconSetup, VirtconNetbootWithNetsvcDisabled) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  std::map<std::string, std::string> arguments;
  arguments["netsvc.disable"] = "true";
  arguments["netsvc.netboot"] = "true";

  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  auto result = console_launcher::GetVirtconArgs(&boot_args);
  ASSERT_TRUE(result.is_ok());
  ASSERT_FALSE(result.value().need_debuglog);
}

// If we don't require a system, we should launch virtcon even if netboot is off.
TEST(VirtconSetup, VirtconDontRequireSystem) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  std::map<std::string, std::string> arguments;
  arguments["netsvc.disable"] = "true";
  arguments["netsvc.netboot"] = "false";
  arguments["devmgr.require_system"] = "false";

  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  auto result = console_launcher::GetVirtconArgs(&boot_args);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().should_launch);
  ASSERT_FALSE(result.value().need_debuglog);
}

// Even if we require a system, we should launch virtcon if netboot is on.
TEST(VirtconSetup, VirtconLaunchWithNetboot) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  std::map<std::string, std::string> arguments;
  arguments["netsvc.disable"] = "false";
  arguments["netsvc.netboot"] = "true";
  arguments["devmgr.require-system"] = "true";

  mock_boot_arguments::Server boot_server(std::move(arguments));
  loop.StartThread();

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  auto result = console_launcher::GetVirtconArgs(&boot_args);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().should_launch);
  ASSERT_TRUE(result.value().need_debuglog);
}
