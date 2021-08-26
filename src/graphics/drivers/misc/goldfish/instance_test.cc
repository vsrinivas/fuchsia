// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish/instance.h"

#include <fuchsia/hardware/acpi/cpp/banjo-mock.h>
#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <fuchsia/hardware/goldfish/pipe/c/banjo.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo-mock.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/wire_messaging.h>

#include <tuple>

#include <zxtest/zxtest.h>
namespace goldfish {

class FakeInstance : public Instance {
 public:
  FakeInstance(zx_device_t* parent) : Instance(parent) {}

  zx_status_t Connect(async_dispatcher_t* dispatcher,
                      fidl::ServerEnd<fuchsia_hardware_goldfish::PipeDevice> server) {
    return fidl::BindSingleInFlightOnly(dispatcher, std::move(server), this);
  }
};

// Test suite creating fake Instance on a mock Pipe device.
class InstanceDeviceTest : public zxtest::Test {
 public:
  InstanceDeviceTest() : zxtest::Test(), loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

  // |zxtest::Test|
  void SetUp() override {
    zx::bti out_bti;
    ASSERT_OK(fake_bti_create(out_bti.reset_and_get_address()));
    ASSERT_OK(out_bti.duplicate(ZX_RIGHT_SAME_RIGHTS, &acpi_bti_));

    zx::vmo out_vmo;
    ASSERT_OK(zx::vmo::create(16 * 1024, 0u, &out_vmo));

    mock_pipe_.ExpectGetBti(ZX_OK, std::move(out_bti));
    mock_pipe_.mock_open().ExpectCallWithMatcher([](int32_t) {});
    mock_pipe_.mock_exec().ExpectCallWithMatcher([](int32_t) {});
    mock_pipe_.mock_destroy().ExpectCallWithMatcher([](int32_t) {});
    mock_pipe_.mock_connect_sysmem().ExpectCallWithMatcher(
        [](const zx::channel&) { return std::make_tuple(ZX_OK); });
    mock_pipe_.mock_register_sysmem_heap().ExpectCallWithMatcher(
        [](uint64_t, const zx::channel&) { return std::make_tuple(ZX_OK); });
    mock_pipe_.mock_set_event().ExpectCallWithMatcher(
        [](int32_t, const zx::event&) { return std::make_tuple(ZX_OK); });
    mock_pipe_.mock_create().ExpectCallWithMatcher([this]() {
      zx::vmo out_vmo;
      zx::vmo::create(16 * 1024, 0u, &out_vmo);
      return std::make_tuple(ZX_OK, ++id_, std::move(out_vmo));
    });
    ddk_.SetProtocol(ZX_PROTOCOL_GOLDFISH_PIPE, mock_pipe_.GetProto());
    dut_ = std::make_unique<FakeInstance>(fake_ddk::FakeParent());

    zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_goldfish::PipeDevice>();
    ASSERT_OK(endpoints.status_value());

    ASSERT_OK(dut_->Connect(loop_.dispatcher(), std::move(endpoints->server)));
    fidl_client_.client_end() = std::move(endpoints->client);
  }

  // |zxtest::Test|
  void TearDown() override {
    loop_.Quit();
    loop_.JoinThreads();
  }

 protected:
  ddk::MockAcpi mock_acpi_;
  ddk::MockGoldfishPipe mock_pipe_;

  fake_ddk::Bind ddk_;
  std::unique_ptr<FakeInstance> dut_;
  fidl::WireSyncClient<fuchsia_hardware_goldfish::PipeDevice> fidl_client_;
  async::Loop loop_;

 private:
  zx::bti acpi_bti_;
  int32_t id_ = 0;
};

TEST_F(InstanceDeviceTest, OpenPipe) {
  dut_->Bind();

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_goldfish::Pipe>();
  ASSERT_TRUE(endpoints.is_ok());

  ASSERT_TRUE(fidl_client_.OpenPipe(std::move(endpoints->server)).ok());
  loop_.RunUntilIdle();
}

TEST_F(InstanceDeviceTest, OpenPipeCloseDutFirst) {
  dut_->Bind();

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_goldfish::Pipe>();
  ASSERT_TRUE(endpoints.is_ok());

  ASSERT_TRUE(fidl_client_.OpenPipe(std::move(endpoints->server)).ok());
  loop_.RunUntilIdle();

  dut_.reset();
  endpoints->client.reset();
}

}  // namespace goldfish
