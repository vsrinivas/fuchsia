// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oom_watcher.h"

#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/eventpair.h>

#include <zxtest/zxtest.h>

class FakePowerManager : public fidl::WireServer<fuchsia_hardware_power_statecontrol::Admin> {
 public:
  FakePowerManager() : reboot_signaled_(false), unexpected_calls_(false) {}
  void PowerFullyOn(PowerFullyOnRequestView view, PowerFullyOnCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Reboot(RebootRequestView view, RebootCompleter::Sync& completer) override {
    if (view->reason == fuchsia_hardware_power_statecontrol::RebootReason::kOutOfMemory) {
      reboot_signaled_ = true;
      completer.ReplySuccess();
    } else {
      unexpected_calls_ = true;
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }
  }

  void RebootToBootloader(RebootToBootloaderRequestView view,
                          RebootToBootloaderCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void RebootToRecovery(RebootToRecoveryRequestView view,
                        RebootToRecoveryCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Poweroff(PoweroffRequestView view, PoweroffCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Mexec(MexecRequestView view, MexecCompleter::Sync& completer) override {
    unexpected_calls_ = true;

    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SuspendToRam(SuspendToRamRequestView view, SuspendToRamCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  bool RebootSignaled() { return this->reboot_signaled_; }
  bool UnexpectedCalls() { return this->unexpected_calls_; }

 private:
  bool reboot_signaled_;
  bool unexpected_calls_;
};

class OomWatcherTest : public zxtest::Test {
 public:
  OomWatcherTest() : loop_(&kAsyncLoopConfigNeverAttachToThread), oom_watcher_() {}

  void SetUp() override {
    ASSERT_OK(loop_.StartThread("oom-test-watcher"));
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_power_statecontrol::Admin>();
    ASSERT_OK(endpoints.status_value());
    power_manager_client_ = std::move(endpoints->client);
    binding_ = fidl::ServerBindingRef<fuchsia_hardware_power_statecontrol::Admin>(
        fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &fake_power_manager_));

    zx::eventpair::create(0, &kernel_oom_event_, &watcher_oom_event_);
    oom_watcher_.WatchForOom(this->loop_.dispatcher(), zx::event(watcher_oom_event_.get()),
                             std::move(this->power_manager_client_));
  }

 protected:
  async::Loop loop_;
  fidl::ClientEnd<fuchsia_hardware_power_statecontrol::Admin> power_manager_client_;
  FakePowerManager fake_power_manager_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_power_statecontrol::Admin>> binding_;
  zx::eventpair kernel_oom_event_;
  zx::eventpair watcher_oom_event_;
  pwrbtn::OomWatcher oom_watcher_;
};

TEST_F(OomWatcherTest, TestOom) {
  this->loop_.RunUntilIdle();
  this->kernel_oom_event_.signal_peer(0, ZX_EVENT_SIGNALED);
  this->loop_.RunUntilIdle();
  ASSERT_EQ(true, this->fake_power_manager_.RebootSignaled());
  ASSERT_EQ(false, this->fake_power_manager_.UnexpectedCalls());
}
