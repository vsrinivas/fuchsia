// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpio.h"

#include <fuchsia/hardware/gpioimpl/cpp/banjo-mock.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/debug.h>
#include <lib/fidl-async/cpp/bind.h>

#include <fbl/alloc_checker.h>

namespace gpio {

class FakeGpio : public GpioDevice {
 public:
  static std::unique_ptr<FakeGpio> Create(const gpio_impl_protocol_t* proto) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeGpio>(&ac, proto);
    if (!ac.check()) {
      zxlogf(ERROR, "FakeGpio::Create: device object alloc failed\n");
      return nullptr;
    }

    return device;
  }

  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    return fidl::BindSingleInFlightOnly(dispatcher, std::move(request), this);
  }

  explicit FakeGpio(const gpio_impl_protocol_t* gpio_impl)
      : GpioDevice(nullptr, const_cast<gpio_impl_protocol_t*>(gpio_impl), 0) {}
};

class GpioTest : public zxtest::Test {
 public:
  void SetUp() override {
    gpio_ = FakeGpio::Create(gpio_impl_.GetProto());
    ASSERT_NOT_NULL(gpio_);
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);

    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client_, &server));
    ASSERT_OK(loop_->StartThread("gpio-test-loop"));
    ASSERT_OK(gpio_->Connect(loop_->dispatcher(), std::move(server)));
  }

  void TearDown() override {
    gpio_impl_.VerifyAndClear();

    loop_->Shutdown();
  }

 protected:
  std::unique_ptr<FakeGpio> gpio_;
  ddk::MockGpioImpl gpio_impl_;
  std::unique_ptr<async::Loop> loop_;
  zx::channel client_;
};

TEST_F(GpioTest, TestFidlAll) {
  fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio> client(std::move(client_));

  gpio_impl_.ExpectRead(ZX_OK, 0, 20);
  auto result_read = client->Read();
  EXPECT_OK(result_read.status());
  EXPECT_EQ(result_read->result.response().value, 20);

  gpio_impl_.ExpectWrite(ZX_OK, 0, 11);
  auto result_write = client->Write(11);
  EXPECT_OK(result_write.status());

  gpio_impl_.ExpectConfigIn(ZX_OK, 0, 0);
  auto result_in = client->ConfigIn(GpioFlags::kPullDown);
  EXPECT_OK(result_in.status());

  gpio_impl_.ExpectConfigOut(ZX_OK, 0, 5);
  auto result_out = client->ConfigOut(5);
  EXPECT_OK(result_out.status());

  gpio_impl_.ExpectSetDriveStrength(ZX_OK, 0, 2000, 2000);
  auto result_drivestrength = client->SetDriveStrength(2000);
  EXPECT_OK(result_drivestrength.status());
  EXPECT_EQ(result_drivestrength->result.response().actual_ds_ua, 2000);

  gpio_impl_.ExpectGetDriveStrength(ZX_OK, 0, 2000);
  auto result_getds = client->GetDriveStrength();
  EXPECT_OK(result_getds.status());
  EXPECT_EQ(result_getds->result.response().result_ua, 2000);
}

TEST_F(GpioTest, TestBanjoSetDriveStrength) {
  uint64_t actual = 0;
  gpio_impl_.ExpectSetDriveStrength(ZX_OK, 0, 3000, 3000);
  EXPECT_OK(gpio_->GpioSetDriveStrength(3000, &actual));
  EXPECT_EQ(actual, 3000);
}

TEST_F(GpioTest, TestBanjoGetDriveStrength) {
  uint64_t result = 0;
  gpio_impl_.ExpectGetDriveStrength(ZX_OK, 0, 3000);
  EXPECT_OK(gpio_->GpioGetDriveStrength(&result));
  EXPECT_EQ(result, 3000);
}

TEST_F(GpioTest, TestCloseReleasesInterrupt) {
  EXPECT_OK(gpio_->DdkOpen(nullptr, 0));

  zx::interrupt interrupt;
  gpio_impl_.ExpectReleaseInterrupt(ZX_OK, 0);

  EXPECT_OK(gpio_->DdkClose(0));

  ASSERT_NO_FAILURES(gpio_impl_.VerifyAndClear());
}

TEST_F(GpioTest, TestOneClient) {
  gpio_impl_.ExpectReleaseInterrupt(ZX_OK, 0).ExpectReleaseInterrupt(ZX_OK, 0);

  EXPECT_OK(gpio_->DdkOpen(nullptr, 0));

  EXPECT_NOT_OK(gpio_->DdkOpen(nullptr, 0));

  EXPECT_OK(gpio_->DdkClose(0));

  EXPECT_OK(gpio_->DdkOpen(nullptr, 0));

  EXPECT_OK(gpio_->DdkClose(0));
}

}  // namespace gpio
