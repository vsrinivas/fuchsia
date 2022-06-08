// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish/instance.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/goldfish/pipe/c/banjo.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo-mock.h>
#include <lib/fake-bti/bti.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/wire_messaging.h>

#include <tuple>

#include <zxtest/zxtest.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/graphics/drivers/misc/goldfish/pipe_device.h"

namespace goldfish {

class FakeInstance : public Instance {
 public:
  FakeInstance(zx_device_t* parent, PipeDevice* pipe_device) : Instance(parent, pipe_device) {}

  zx_status_t Connect(async_dispatcher_t* dispatcher,
                      fidl::ServerEnd<fuchsia_hardware_goldfish::PipeDevice> server) {
    return fidl::BindSingleInFlightOnly(dispatcher, std::move(server), this);
  }
};

class FakePipeDevice : public PipeDevice {
 public:
  FakePipeDevice(zx_device_t* parent, acpi::Client client)
      : PipeDevice(parent, std::move(client)) {}

  zx_status_t Create(int32_t* out_id, zx::vmo* out_vmo) {
    zx_status_t status = zx::vmo::create(16 * 1024, 0u, out_vmo);
    (*out_id)++;
    return status;
  }

  zx_status_t GetBti(zx::bti* out_bti) { return fake_bti_create(out_bti->reset_and_get_address()); }
};

// Test suite creating fake Instance on a mock Pipe device.
class InstanceDeviceTest : public zxtest::Test {
 public:
  InstanceDeviceTest()
      : zxtest::Test(),
        fake_root_(MockDevice::FakeRootParent()),
        loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

  // |zxtest::Test|
  void SetUp() override {
    auto acpi_result = mock_acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(acpi_result.status_value());

    pipe_device_ =
        std::make_unique<FakePipeDevice>(fake_root_.get(), std::move(acpi_result.value()));
    auto dut = std::make_unique<FakeInstance>(fake_root_.get(), pipe_device_.get());
    ASSERT_OK(dut->Bind());
    // dut is now managed by the Driver Framework so we release the unique
    // pointer. Note that pipe_device_ is not managed by the framework so we
    // retain the unique pointer as an instance variable so that it is freed at
    // the end of the test.
    dut_ = dut.release();

    zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_goldfish::PipeDevice>();
    ASSERT_OK(endpoints.status_value());

    ASSERT_OK(dut_->Connect(loop_.dispatcher(), std::move(endpoints->server)));
    fidl_client_.Bind(std::move(endpoints->client));
  }

  // |zxtest::Test|
  void TearDown() override {
    loop_.Quit();
    loop_.JoinThreads();
  }

 protected:
  acpi::mock::Device mock_acpi_;

  std::unique_ptr<FakePipeDevice> pipe_device_;
  FakeInstance* dut_;
  std::shared_ptr<MockDevice> fake_root_;

  fidl::WireSyncClient<fuchsia_hardware_goldfish::PipeDevice> fidl_client_;
  async::Loop loop_;

 private:
  zx::bti acpi_bti_;
};

TEST_F(InstanceDeviceTest, OpenPipe) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_goldfish::Pipe>();
  ASSERT_TRUE(endpoints.is_ok());

  ASSERT_TRUE(fidl_client_->OpenPipe(std::move(endpoints->server)).ok());
  loop_.RunUntilIdle();
}

TEST_F(InstanceDeviceTest, OpenPipeCloseDutFirst) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_goldfish::Pipe>();
  ASSERT_TRUE(endpoints.is_ok());

  ASSERT_TRUE(fidl_client_->OpenPipe(std::move(endpoints->server)).ok());
  loop_.RunUntilIdle();

  endpoints->client.reset();
}

}  // namespace goldfish
