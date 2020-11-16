// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

TEST(ArgsTest, CheckDisabled) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  loop.StartThread();
  Arguments args;

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  mock_boot_arguments::Server boot_server;
  std::map<std::string, std::string> arguments;

  arguments["virtcon.disable"] = "false";
  boot_server = mock_boot_arguments::Server(std::move(arguments));
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
  ASSERT_FALSE(args.disable);

  arguments["virtcon.disable"] = "true";
  boot_server = mock_boot_arguments::Server(std::move(arguments));
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
  ASSERT_TRUE(args.disable);
}

TEST(ArgsTest, CheckBootBools) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  loop.StartThread();

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  mock_boot_arguments::Server boot_server;
  std::map<std::string, std::string> arguments;

  arguments["virtcon.disable"] = "true";
  arguments["virtcon.keep-log-visible"] = "true";
  arguments["virtcon.keyrepeat"] = "true";
  arguments["virtcon.hide-on-boot"] = "true";
  boot_server = mock_boot_arguments::Server(std::move(arguments));
  boot_server.CreateClient(loop.dispatcher(), &boot_args);

  Arguments args;
  ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);

  ASSERT_TRUE(args.disable);
  ASSERT_TRUE(args.repeat_keys);
  ASSERT_TRUE(args.keep_log_visible);
  ASSERT_TRUE(args.hide_on_boot);
}

TEST(ArgsTest, CheckColorScheme) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  loop.StartThread();
  Arguments args;

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  mock_boot_arguments::Server boot_server;
  std::map<std::string, std::string> arguments;

  // Default scheme.
  {
    arguments = std::map<std::string, std::string>();
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x0F);
    ASSERT_EQ(args.color_scheme->back, 0x00);
  }

  // Dark Scheme.
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.colorscheme"] = "dark";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x0F);
    ASSERT_EQ(args.color_scheme->back, 0x00);
  }

  // Light Scheme.
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.colorscheme"] = "light";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x00);
    ASSERT_EQ(args.color_scheme->back, 0x0F);
  }

  // Special Scheme.
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.colorscheme"] = "special";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x0F);
    ASSERT_EQ(args.color_scheme->back, 0x04);
  }

  // Nonsense string == default scheme
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.colorscheme"] = "myamazingtheme";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x0F);
    ASSERT_EQ(args.color_scheme->back, 0x00);
  }
}

TEST(ArgsTest, CheckFont) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  loop.StartThread();
  Arguments args;

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  mock_boot_arguments::Server boot_server;
  std::map<std::string, std::string> arguments;

  // Default
  {
    arguments = std::map<std::string, std::string>();
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.font, &gfx_font_9x16);
  }

  // 9x16
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.font"] = "9x16";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.font, &gfx_font_9x16);
  }

  // 18x32
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.font"] = "18x32";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.font, &gfx_font_18x32);
  }

  // Nonsense string == default
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.font"] = "ONEMILLION";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.font, &gfx_font_9x16);
  }
}

TEST(ArgsTest, CheckKeymap) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  loop.StartThread();
  Arguments args;

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  mock_boot_arguments::Server boot_server;
  std::map<std::string, std::string> arguments;

  // Default
  {
    arguments = std::map<std::string, std::string>();
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.keymap, qwerty_map);
  }

  // qwerty
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.keymap"] = "qwerty";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.keymap, qwerty_map);
  }

  // dvorak
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.keymap"] = "dvorak";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.keymap, dvorak_map);
  }

  // nonsense string == default.
  {
    arguments = std::map<std::string, std::string>();
    arguments["virtcon.keymap"] = "randomizedlayout";
    boot_server = mock_boot_arguments::Server(std::move(arguments));
    boot_server.CreateClient(loop.dispatcher(), &boot_args);

    ASSERT_EQ(ParseArgs(boot_args, &args), ZX_OK);
    ASSERT_EQ(args.keymap, qwerty_map);
  }
}
