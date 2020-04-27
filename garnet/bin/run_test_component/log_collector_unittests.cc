// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/logger.h>
#include <zircon/errors.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "garnet/bin/run_test_component/log_collector.h"

TEST(LogCollector, DoubleBind) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  run::LogCollector collector([](const fuchsia::logger::LogMessage& /*unused*/) {});
  fuchsia::logger::LogListenerSafePtr ptr1;
  ASSERT_EQ(ZX_OK, collector.Bind(ptr1.NewRequest(), loop.dispatcher()));
  fuchsia::logger::LogListenerSafePtr ptr2;
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, collector.Bind(ptr2.NewRequest(), loop.dispatcher()));
}

TEST(LogCollector, NotifyWhenUnbound) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  run::LogCollector collector([](const fuchsia::logger::LogMessage& /*unused*/) {});
  bool called = false;
  collector.NotifyOnUnBind([&]() { called = true; });
  ASSERT_TRUE(called);
}

TEST(LogCollector, NotifyWhenBound) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  run::LogCollector collector([](const fuchsia::logger::LogMessage& /*unused*/) {});
  fuchsia::logger::LogListenerSafePtr ptr;
  ASSERT_EQ(ZX_OK, collector.Bind(ptr.NewRequest(), loop.dispatcher()));
  bool called = false;
  collector.NotifyOnUnBind([&]() { called = true; });
  ASSERT_FALSE(called);
  ptr.Unbind();
  loop.RunUntilIdle();
  ASSERT_TRUE(called);
}

TEST(LogCollector, MultipleNotifyWhenBound) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  run::LogCollector collector([](const fuchsia::logger::LogMessage& /*unused*/) {});
  fuchsia::logger::LogListenerSafePtr ptr;
  ASSERT_EQ(ZX_OK, collector.Bind(ptr.NewRequest(), loop.dispatcher()));
  bool called1 = false;
  collector.NotifyOnUnBind([&]() { called1 = true; });
  bool called2 = false;
  collector.NotifyOnUnBind([&]() { called2 = true; });
  ASSERT_FALSE(called1);
  ASSERT_FALSE(called2);
  ptr.Unbind();
  loop.RunUntilIdle();
  ASSERT_TRUE(called2);
  ASSERT_TRUE(called1);
}

fuchsia::logger::LogMessage create_msg(std::string msg) {
  return fuchsia::logger::LogMessage{.msg = std::move(msg)};
}

void dummy_callback() {}

TEST(LogCollector, CollectLogMessages) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::vector<std::string> msgs;
  run::LogCollector collector(
      [&](const fuchsia::logger::LogMessage& log) { msgs.push_back(log.msg); });

  fuchsia::logger::LogListenerSafePtr ptr;
  ASSERT_EQ(ZX_OK, collector.Bind(ptr.NewRequest(), loop.dispatcher()));

  ptr->Log(create_msg("msg 1"), dummy_callback);
  ptr->Log(create_msg("msg 2"), dummy_callback);
  ptr->LogMany({create_msg("msg 3"), create_msg("msg 4"), create_msg("msg 5")}, dummy_callback);

  loop.RunUntilIdle();
  std::vector<std::string> expected = {
      "msg 1", "msg 2", "msg 3", "msg 4", "msg 5",
  };
  ASSERT_EQ(msgs, expected);
  ptr->Log(create_msg("msg 6"), dummy_callback);
  collector.NotifyOnUnBind([&]() { loop.Quit(); });
  ptr.Unbind();
  loop.Run();
  expected.push_back("msg 6");

  ASSERT_EQ(msgs, expected);
}
