// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/device.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/fidl/llcpp/connect_service.h>

#include <zxtest/zxtest.h>

#include "lib/fidl/llcpp/wire_messaging.h"
#include "src/devices/board/drivers/x86/acpi/test/device.h"
#include "src/devices/board/drivers/x86/acpi/test/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

class AcpiDeviceTest : public zxtest::Test {
 public:
  AcpiDeviceTest() : mock_root_(MockDevice::FakeRootParent()) {}

  void SetUp() override { acpi_.SetDeviceRoot(std::make_unique<acpi::test::Device>("\\")); }

  void SetUpFidlServer(std::unique_ptr<acpi::Device> device) {
    ASSERT_OK(device->DdkAdd("test-acpi-device"));

    // Give mock_ddk ownership of the device.
    zx_device_t* dev = device.release()->zxdev();

    ddk::AcpiProtocolClient acpi(dev);
    ASSERT_TRUE(acpi.is_valid());

    fidl::ClientEnd<fuchsia_hardware_acpi::Device> client_end;
    auto server_end = fidl::CreateEndpoints(&client_end);
    ASSERT_OK(server_end.status_value());

    acpi.ConnectServer(server_end->TakeChannel());
    fidl_client_ = fidl::WireSyncClient<fuchsia_hardware_acpi::Device>(std::move(client_end));
  }

 protected:
  std::shared_ptr<MockDevice> mock_root_;
  acpi::test::MockAcpi acpi_;
  fidl::WireSyncClient<fuchsia_hardware_acpi::Device> fidl_client_;
};

TEST_F(AcpiDeviceTest, TestBanjoConnectServer) {
  auto device =
      std::make_unique<acpi::Device>(&acpi_, mock_root_.get(), ACPI_ROOT_OBJECT, mock_root_.get());
  SetUpFidlServer(std::move(device));

  auto result = fidl_client_.GetBusId();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_err());
  ASSERT_EQ(result.value().result.err(), ZX_ERR_BAD_STATE);
}

TEST_F(AcpiDeviceTest, TestBanjoConnectServerTwice) {
  auto device =
      std::make_unique<acpi::Device>(&acpi_, mock_root_.get(), ACPI_ROOT_OBJECT, mock_root_.get());
  SetUpFidlServer(std::move(device));
  {
    auto result = fidl_client_.GetBusId();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result.value().result.is_err());
    ASSERT_EQ(result.value().result.err(), ZX_ERR_BAD_STATE);
  }

  // Connect again and make sure it still works.
  ddk::AcpiProtocolClient acpi(mock_root_->GetLatestChild());
  ASSERT_TRUE(acpi.is_valid());
  fidl::ClientEnd<fuchsia_hardware_acpi::Device> client_end2;
  auto server_end = fidl::CreateEndpoints(&client_end2);
  ASSERT_OK(server_end.status_value());

  acpi.ConnectServer(server_end->TakeChannel());

  auto fidl_client2 = fidl::WireSyncClient<fuchsia_hardware_acpi::Device>(std::move(client_end2));
  {
    auto result = fidl_client2.GetBusId();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result.value().result.is_err());
    ASSERT_EQ(result.value().result.err(), ZX_ERR_BAD_STATE);
  }
}

TEST_F(AcpiDeviceTest, TestGetBusId) {
  auto device =
      std::make_unique<acpi::Device>(&acpi_, mock_root_.get(), ACPI_ROOT_OBJECT, mock_root_.get(),
                                     std::vector<uint8_t>(), acpi::BusType::kI2c, 37);
  SetUpFidlServer(std::move(device));

  auto result = fidl_client_.GetBusId();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_response());
  ASSERT_EQ(result.value().result.response().bus_id, 37);
}
