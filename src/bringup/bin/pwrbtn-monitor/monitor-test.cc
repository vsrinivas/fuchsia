// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/pwrbtn-monitor/monitor.h"

#include <fidl/fuchsia.power.button/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>

#include <zxtest/zxtest.h>

using PwrAction = fuchsia_power_button::wire::Action;

class MonitorTest : public zxtest::Test {
 public:
  MonitorTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    ASSERT_OK(loop_.StartThread("test-fidl-thread"));
    auto endpoints = fidl::CreateEndpoints<fuchsia_power_button::Monitor>();
    ASSERT_OK(endpoints.status_value());

    client_ = fidl::WireSyncClient<fuchsia_power_button::Monitor>(std::move(endpoints->client));
    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &monitor_);
  }

 protected:
  async::Loop loop_;
  pwrbtn::PowerButtonMonitor monitor_;
  fidl::WireSyncClient<fuchsia_power_button::Monitor> client_;
};

TEST_F(MonitorTest, TestSetAction) {
  client_.SetAction(PwrAction::kIgnore);
  auto resp = client_.GetAction();
  ASSERT_OK(resp.status());
  ASSERT_EQ(resp->action, PwrAction::kIgnore);

  ASSERT_OK(monitor_.DoAction());
}

TEST_F(MonitorTest, TestGetActionDefault) {
  auto resp = client_.GetAction();
  ASSERT_OK(resp.status());
  ASSERT_EQ(resp->action, PwrAction::kShutdown);
}

TEST_F(MonitorTest, TestShutdownFailsWithNoService) { ASSERT_NOT_OK(monitor_.DoAction()); }
