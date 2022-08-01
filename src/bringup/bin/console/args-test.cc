// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/console/args.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/wire/connect_service.h>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

namespace {

TEST(ConsoleArgsTestCase, BootArgsPrecedence) {
  console_config::Config config{{
      .allowed_log_tags = {"qux"},
      .denied_log_tags = {"foo", "baz"},
  }};
  mock_boot_arguments::Server mock_args(
      {{"console.allowed_log_tags", "foo,bar"}, {"console.denied_log_tags", "qux"}});

  async::Loop loop{&kAsyncLoopConfigNeverAttachToThread};
  loop.StartThread("mock-boot-args");

  fidl::WireSyncClient<fuchsia_boot::Arguments> args_client;
  mock_args.CreateClient(loop.dispatcher(), &args_client);

  Options opts;
  ASSERT_OK(ParseArgs(std::move(config), args_client, &opts));

  const std::vector<std::string> kAllowedExpected{"qux", "foo", "bar"};
  ASSERT_EQ(opts.allowed_log_tags, kAllowedExpected);
  const std::vector<std::string> kDeniedExpected{"baz", "qux"};
  ASSERT_EQ(opts.denied_log_tags, kDeniedExpected);
}

}  // namespace
