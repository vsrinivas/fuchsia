// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpioutil.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl-utils/bind.h>
#include <lib/mock-function/mock-function.h>

#include <zxtest/zxtest.h>

namespace {

using fuchsia_hardware_gpio::Gpio;

class FakeGpio : public fidl::WireInterface<Gpio> {
 public:
  explicit FakeGpio() {}

  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    return fidl::BindSingleInFlightOnly(dispatcher, std::move(request), this);
  }

  void ConfigIn(fuchsia_hardware_gpio::wire::GpioFlags flags, ConfigInCompleter::Sync& completer) {
    if (flags != fuchsia_hardware_gpio::wire::GpioFlags::kNoPull) {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }
    mock_config_in_.Call();
    completer.ReplySuccess();
  }
  void ConfigOut(uint8_t initial_value, ConfigOutCompleter::Sync& completer) {
    if (initial_value != 3) {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }
    mock_config_out_.Call();
    completer.ReplySuccess();
  }
  void Read(ReadCompleter::Sync& completer) {
    mock_read_.Call();
    completer.ReplySuccess(5);
  }
  void Write(uint8_t value, WriteCompleter::Sync& completer) {
    if (value != 7) {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }
    mock_write_.Call();
    completer.ReplySuccess();
  }
  void SetDriveStrength(uint64_t ds_ua, SetDriveStrengthCompleter::Sync& completer) {
    if (ds_ua != 2000) {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }
    mock_set_drive_strength_.Call();
    completer.ReplySuccess(2000);
  }

  mock_function::MockFunction<zx_status_t>& MockConfigIn() { return mock_config_in_; }
  mock_function::MockFunction<zx_status_t>& MockConfigOut() { return mock_config_out_; }
  mock_function::MockFunction<zx_status_t>& MockRead() { return mock_read_; }
  mock_function::MockFunction<zx_status_t>& MockWrite() { return mock_write_; }
  mock_function::MockFunction<zx_status_t>& MockSetDriveStrength() {
    return mock_set_drive_strength_;
  }

 private:
  mock_function::MockFunction<zx_status_t> mock_config_in_;
  mock_function::MockFunction<zx_status_t> mock_config_out_;
  mock_function::MockFunction<zx_status_t> mock_read_;
  mock_function::MockFunction<zx_status_t> mock_write_;
  mock_function::MockFunction<zx_status_t> mock_set_drive_strength_;
};

class GpioUtilTest : public zxtest::Test {
 public:
  void SetUp() override {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    gpio_ = std::make_unique<FakeGpio>();

    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client_, &server));
    ASSERT_OK(loop_->StartThread("gpioutil-test-loop"));
    ASSERT_OK(gpio_->Connect(loop_->dispatcher(), std::move(server)));
  }

  void TearDown() override {
    gpio_->MockConfigIn().VerifyAndClear();
    gpio_->MockConfigOut().VerifyAndClear();
    gpio_->MockRead().VerifyAndClear();
    gpio_->MockWrite().VerifyAndClear();
    gpio_->MockSetDriveStrength().VerifyAndClear();

    loop_->Shutdown();
  }

 protected:
  std::unique_ptr<async::Loop> loop_;
  zx::channel client_;
  std::unique_ptr<FakeGpio> gpio_;
};

TEST_F(GpioUtilTest, ReadTest) {
  int argc = 3;
  const char* argv[] = {"gpioutil", "r", "some_path"};

  GpioFunc func;
  uint8_t write_value, out_value;
  uint64_t ds_ua;
  fuchsia_hardware_gpio::wire::GpioFlags in_flag;
  EXPECT_EQ(
      ParseArgs(argc, const_cast<char**>(argv), &func, &write_value, &in_flag, &out_value, &ds_ua),
      0);
  EXPECT_EQ(func, 0);
  EXPECT_EQ(write_value, 0);
  EXPECT_EQ(in_flag, fuchsia_hardware_gpio::wire::GpioFlags::kNoPull);
  EXPECT_EQ(out_value, 0);
  EXPECT_EQ(ds_ua, 0);

  gpio_->MockRead().ExpectCall(ZX_OK);
  EXPECT_EQ(ClientCall(fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio>(std::move(client_)), func,
                       write_value, in_flag, out_value, ds_ua),
            0);
}

TEST_F(GpioUtilTest, WriteTest) {
  int argc = 4;
  const char* argv[] = {"gpioutil", "w", "some_path", "7"};

  GpioFunc func;
  uint8_t write_value, out_value;
  uint64_t ds_ua;
  fuchsia_hardware_gpio::wire::GpioFlags in_flag;
  EXPECT_EQ(
      ParseArgs(argc, const_cast<char**>(argv), &func, &write_value, &in_flag, &out_value, &ds_ua),
      0);
  EXPECT_EQ(func, 1);
  EXPECT_EQ(write_value, 7);
  EXPECT_EQ(in_flag, fuchsia_hardware_gpio::wire::GpioFlags::kNoPull);
  EXPECT_EQ(out_value, 0);
  EXPECT_EQ(ds_ua, 0);

  gpio_->MockWrite().ExpectCall(ZX_OK);
  EXPECT_EQ(ClientCall(fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio>(std::move(client_)), func,
                       write_value, in_flag, out_value, ds_ua),
            0);
}

TEST_F(GpioUtilTest, ConfigInTest) {
  int argc = 4;
  const char* argv[] = {"gpioutil", "i", "some_path", "2"};

  GpioFunc func;
  uint8_t write_value, out_value;
  uint64_t ds_ua;
  fuchsia_hardware_gpio::wire::GpioFlags in_flag;
  EXPECT_EQ(
      ParseArgs(argc, const_cast<char**>(argv), &func, &write_value, &in_flag, &out_value, &ds_ua),
      0);
  EXPECT_EQ(func, 2);
  EXPECT_EQ(write_value, 0);
  EXPECT_EQ(in_flag, fuchsia_hardware_gpio::wire::GpioFlags::kNoPull);
  EXPECT_EQ(out_value, 0);
  EXPECT_EQ(ds_ua, 0);

  gpio_->MockConfigIn().ExpectCall(ZX_OK);
  EXPECT_EQ(ClientCall(fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio>(std::move(client_)), func,
                       write_value, in_flag, out_value, ds_ua),
            0);
}

TEST_F(GpioUtilTest, ConfigOutTest) {
  int argc = 4;
  const char* argv[] = {"gpioutil", "o", "some_path", "3"};

  GpioFunc func;
  uint8_t write_value, out_value;
  uint64_t ds_ua;
  fuchsia_hardware_gpio::wire::GpioFlags in_flag;
  EXPECT_EQ(
      ParseArgs(argc, const_cast<char**>(argv), &func, &write_value, &in_flag, &out_value, &ds_ua),
      0);
  EXPECT_EQ(func, 3);
  EXPECT_EQ(write_value, 0);
  EXPECT_EQ(in_flag, fuchsia_hardware_gpio::wire::GpioFlags::kNoPull);
  EXPECT_EQ(out_value, 3);
  EXPECT_EQ(ds_ua, 0);

  gpio_->MockConfigOut().ExpectCall(ZX_OK);
  EXPECT_EQ(ClientCall(fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio>(std::move(client_)), func,
                       write_value, in_flag, out_value, ds_ua),
            0);
}

TEST_F(GpioUtilTest, SetDriveStrengthTest) {
  int argc = 4;
  const char* argv[] = {"gpioutil", "d", "some_path", "2000"};

  GpioFunc func;
  uint8_t write_value, out_value;
  uint64_t ds_ua;
  fuchsia_hardware_gpio::wire::GpioFlags in_flag;
  EXPECT_EQ(
      ParseArgs(argc, const_cast<char**>(argv), &func, &write_value, &in_flag, &out_value, &ds_ua),
      0);
  EXPECT_EQ(func, 4);
  EXPECT_EQ(write_value, 0);
  EXPECT_EQ(in_flag, fuchsia_hardware_gpio::wire::GpioFlags::kNoPull);
  EXPECT_EQ(out_value, 0);
  EXPECT_EQ(ds_ua, 2000);

  gpio_->MockSetDriveStrength().ExpectCall(ZX_OK);
  EXPECT_EQ(ClientCall(fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio>(std::move(client_)), func,
                       write_value, in_flag, out_value, ds_ua),
            0);
}

}  // namespace
