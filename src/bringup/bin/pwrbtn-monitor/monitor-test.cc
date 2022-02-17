// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/pwrbtn-monitor/monitor.h"

#include <fidl/fuchsia.power.button/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>

#include <zxtest/zxtest.h>

using PwrAction = fuchsia_power_button::wire::Action;
using PwrButtonEvent = fuchsia_power_button::wire::PowerButtonEvent;

class MonitorTest : public zxtest::Test {
 public:
  MonitorTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    ASSERT_OK(loop_.StartThread("test-fidl-thread"));
    auto endpoints = fidl::CreateEndpoints<fuchsia_power_button::Monitor>();
    ASSERT_OK(endpoints.status_value());

    client_ = fidl::WireSyncClient<fuchsia_power_button::Monitor>(std::move(endpoints->client));
    binding_ = fidl::ServerBindingRef<fuchsia_power_button::Monitor>(
        fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &monitor_));
  }

 protected:
  async::Loop loop_;
  pwrbtn::PowerButtonMonitor monitor_;
  fidl::WireSyncClient<fuchsia_power_button::Monitor> client_;
  std::optional<fidl::ServerBindingRef<fuchsia_power_button::Monitor>> binding_;
};

class EventHandler : public fidl::WireSyncEventHandler<fuchsia_power_button::Monitor> {
 public:
  EventHandler() = default;

  void OnButtonEvent(
      fidl::WireEvent<fuchsia_power_button::Monitor::OnButtonEvent>* event) override {
    e = event->event;
  }

  PwrButtonEvent e = PwrButtonEvent::Unknown();

  zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }
};

TEST_F(MonitorTest, TestSetAction) {
  client_->SetAction(PwrAction::kIgnore);
  auto resp = client_->GetAction();
  ASSERT_OK(resp.status());
  ASSERT_EQ(resp->action, PwrAction::kIgnore);

  ASSERT_OK(monitor_.DoAction());
}

TEST_F(MonitorTest, TestGetActionDefault) {
  auto resp = client_->GetAction();
  ASSERT_OK(resp.status());
  ASSERT_EQ(resp->action, PwrAction::kShutdown);
}

TEST_F(MonitorTest, TestSendButtonEvent) {
  EventHandler event_handler;
  ASSERT_TRUE(event_handler.e.IsUnknown());
  ASSERT_NE(event_handler.e, PwrButtonEvent::kPress);

  ASSERT_OK(monitor_.SendButtonEvent(binding_.value(), PwrButtonEvent::kPress));
  ASSERT_OK(client_.HandleOneEvent(event_handler));
  ASSERT_EQ(event_handler.e, PwrButtonEvent::kPress);

  ASSERT_OK(monitor_.SendButtonEvent(binding_.value(), PwrButtonEvent::kRelease));
  ASSERT_OK(client_.HandleOneEvent(event_handler));
  ASSERT_EQ(event_handler.e, PwrButtonEvent::kRelease);
}

TEST_F(MonitorTest, TestShutdownFailsWithNoService) { ASSERT_NOT_OK(monitor_.DoAction()); }
