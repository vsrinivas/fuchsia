// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pwmctl.h"

#include <lib/async-loop/cpp/loop.h>

#include <zxtest/zxtest.h>

namespace pwmctl {

namespace {

constexpr char kBinaryName[] = "pwmctl";
constexpr char kDevPath[] = "/dev/class/pwm/000";

}  // namespace

class FakePwmDevice : public fidl::WireServer<fuchsia_hardware_pwm::Pwm> {
 public:
  FakePwmDevice() : loop_(&kAsyncLoopConfigNeverAttachToThread) {
    loop_.StartThread("pwmctl-test-thread");
  }

  fidl::ClientEnd<fuchsia_hardware_pwm::Pwm> GetPwmClient() {
    fidl::ClientEnd<fuchsia_hardware_pwm::Pwm> client;
    fidl::ServerEnd<fuchsia_hardware_pwm::Pwm> server;

    if (zx::channel::create(0, &client.channel(), &server.channel()) != ZX_OK) {
      return {};
    }

    fidl::BindServer(loop_.dispatcher(), std::move(server), this);
    return client;
  }

  void GetConfig(GetConfigRequestView request, GetConfigCompleter::Sync& completer) override {
    get_config_count_++;
    completer.ReplySuccess(config_);
  }

  void SetConfig(SetConfigRequestView request, SetConfigCompleter::Sync& completer) override {
    set_config_count_++;
    config_ = request->config;
    completer.ReplySuccess();
  }

  void Enable(EnableRequestView request, EnableCompleter::Sync& completer) override {
    enable_count_++;
    completer.ReplySuccess();
  }

  void Disable(DisableRequestView request, DisableCompleter::Sync& completer) override {
    disable_count_++;
    completer.ReplySuccess();
  }

  // Accessors.
  uint32_t GetConfigCount() const { return get_config_count_; }
  uint32_t SetConfigCount() const { return set_config_count_; }
  uint32_t EnableCount() const { return enable_count_; }
  uint32_t DisableCount() const { return disable_count_; }
  fuchsia_hardware_pwm::wire::PwmConfig Config() const { return config_; }

 private:
  uint32_t get_config_count_ = 0;
  uint32_t set_config_count_ = 0;
  uint32_t enable_count_ = 0;
  uint32_t disable_count_ = 0;
  fuchsia_hardware_pwm::wire::PwmConfig config_ = {
      .polarity = false, .period_ns = 0xDEADBEEF, .duty_cycle = 10.0};
  async::Loop loop_;
};

TEST(PwmCtlTest, Enable) {
  FakePwmDevice fake_pwm;

  char const* args[] = {kBinaryName, kDevPath, "enable"};

  EXPECT_OK(run(countof(args), args, fake_pwm.GetPwmClient()));

  EXPECT_EQ(fake_pwm.EnableCount(), 1);
  EXPECT_EQ(fake_pwm.DisableCount(), 0);
  EXPECT_EQ(fake_pwm.SetConfigCount(), 0);
  EXPECT_EQ(fake_pwm.GetConfigCount(), 0);
}

TEST(PwmCtlTest, Disable) {
  FakePwmDevice fake_pwm;

  char const* args[] = {kBinaryName, kDevPath, "disable"};

  EXPECT_OK(run(countof(args), args, fake_pwm.GetPwmClient()));

  EXPECT_EQ(fake_pwm.EnableCount(), 0);
  EXPECT_EQ(fake_pwm.DisableCount(), 1);
  EXPECT_EQ(fake_pwm.SetConfigCount(), 0);
  EXPECT_EQ(fake_pwm.GetConfigCount(), 0);
}

TEST(PwmCtlTest, SetConfig) {
  FakePwmDevice fake_pwm;

  const std::string set_config = "config";
  char const* args[] = {kBinaryName, kDevPath, "config", "1", "1234", "45.0"};

  EXPECT_OK(run(countof(args), args, fake_pwm.GetPwmClient()));

  EXPECT_EQ(fake_pwm.EnableCount(), 0);
  EXPECT_EQ(fake_pwm.DisableCount(), 0);
  EXPECT_EQ(fake_pwm.SetConfigCount(), 1);
  EXPECT_EQ(fake_pwm.GetConfigCount(), 0);

  auto config = fake_pwm.Config();
  EXPECT_EQ(config.polarity, true);
  EXPECT_EQ(config.period_ns, 1234);
  EXPECT_EQ(config.duty_cycle, 45.0);
}

TEST(PwmCtlTest, InvalidCommand) {
  FakePwmDevice fake_pwm;

  char const* args[] = {kBinaryName, kDevPath, "bad-argument"};

  EXPECT_NOT_OK(run(countof(args), args, fake_pwm.GetPwmClient()));

  EXPECT_EQ(fake_pwm.EnableCount(), 0);
  EXPECT_EQ(fake_pwm.DisableCount(), 0);
  EXPECT_EQ(fake_pwm.SetConfigCount(), 0);
  EXPECT_EQ(fake_pwm.GetConfigCount(), 0);
}

TEST(PwmCtlTest, SetConfigArgs) {
  FakePwmDevice fake_pwm;

  char const* bad_polarity[] = {kBinaryName, kDevPath, "config", "2", "1234", "45.0"};
  EXPECT_NOT_OK(run(countof(bad_polarity), bad_polarity, fake_pwm.GetPwmClient()));

  char const* negative_period[] = {kBinaryName, kDevPath, "config", "1", "-12", "45.0"};
  EXPECT_NOT_OK(run(countof(negative_period), negative_period, fake_pwm.GetPwmClient()));

  char const* bad_duty_cycle[] = {kBinaryName, kDevPath, "config", "1", "1234", "101.0"};
  EXPECT_NOT_OK(run(countof(bad_duty_cycle), bad_duty_cycle, fake_pwm.GetPwmClient()));

  char const* negative_duty_cycle[] = {kBinaryName, kDevPath, "config", "1", "1234", "-10.0"};
  EXPECT_NOT_OK(run(countof(negative_duty_cycle), negative_duty_cycle, fake_pwm.GetPwmClient()));

  EXPECT_EQ(fake_pwm.EnableCount(), 0);
  EXPECT_EQ(fake_pwm.DisableCount(), 0);
  EXPECT_EQ(fake_pwm.SetConfigCount(), 0);
  EXPECT_EQ(fake_pwm.GetConfigCount(), 0);
}

}  // namespace pwmctl
