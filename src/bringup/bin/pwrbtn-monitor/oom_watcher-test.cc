// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oom_watcher.h"

#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/eventpair.h>

#include <atomic>

#include <zxtest/zxtest.h>

class FakePowerManager : public fidl::WireServer<fuchsia_hardware_power_statecontrol::Admin> {
 public:
  FakePowerManager() : reboot_signaled_(false), unexpected_calls_(false) {}
  void PowerFullyOn(PowerFullyOnCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Reboot(RebootRequestView view, RebootCompleter::Sync& completer) override {
    if (view->reason == fuchsia_hardware_power_statecontrol::RebootReason::kOutOfMemory) {
      reboot_signaled_ = true;
      completer.ReplySuccess();
      loop_->Quit();
    } else {
      unexpected_calls_ = true;
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }
  }

  void RebootToBootloader(RebootToBootloaderCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void RebootToRecovery(RebootToRecoveryCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Poweroff(PoweroffCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Mexec(MexecRequestView view, MexecCompleter::Sync& completer) override {
    unexpected_calls_ = true;

    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SuspendToRam(SuspendToRamCompleter::Sync& completer) override {
    unexpected_calls_ = true;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  bool RebootSignaled() { return this->reboot_signaled_; }
  bool UnexpectedCalls() { return this->unexpected_calls_; }
  void SetLoop(async::Loop* loop) { loop_ = loop; }

 private:
  std::atomic<bool> reboot_signaled_;
  std::atomic<bool> unexpected_calls_;
  async::Loop* loop_;
};

class OomWatcherTest : public zxtest::Test {
 public:
  OomWatcherTest() : loop_(&kAsyncLoopConfigNeverAttachToThread), oom_watcher_() {}

  void SetUp() override {
    ASSERT_OK(loop_.StartThread("oom-test-watcher"));
    // Give the fake a reference to the loop so it can quit the loop once it
    // receives the shutdown signal from the code under test.
    fake_power_manager_.SetLoop(&loop_);
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
  while (loop_.GetState() != ASYNC_LOOP_QUIT) {
    this->loop_.RunUntilIdle();
  }
  ASSERT_EQ(true, this->fake_power_manager_.RebootSignaled());
  ASSERT_EQ(false, this->fake_power_manager_.UnexpectedCalls());
}
