// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/driver_development.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <zxtest/zxtest.h>

#include "multiple_device_test.h"

class DriverDevelopmentTest : public MultipleDeviceTestCase {};

TEST_F(DriverDevelopmentTest, DeviceInfo) {
  std::vector<fbl::RefPtr<Device>> devices;
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  devices.push_back(device(parent_index)->device);

  devices[0]->flags = DEV_CTX_BOUND;
  auto arena = std::make_unique<fidl::Arena<512>>();
  auto result = GetDeviceInfo(*arena, devices);
  ASSERT_EQ(ZX_OK, result.status_value());

  auto endpoints = fidl::CreateEndpoints<fuchsia_driver_development::DeviceInfoIterator>();
  auto iterator = std::make_unique<DeviceInfoIterator>(std::move(arena), std::move(*result));

  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), std::move(iterator));

  auto client = fidl::WireClient(std::move(endpoints->client), loop.dispatcher());

  bool was_called = false;
  client->GetNext().Then(
      [&was_called](
          fidl::WireUnownedResult<::fuchsia_driver_development::DeviceInfoIterator::GetNext>&
              result) {
        was_called = true;
        auto count = result->drivers.count();
        ASSERT_EQ(count, 1);

        ASSERT_EQ(std::string(result->drivers[0].topological_path().get()),
                  std::string("/dev/sys/platform-bus/parent-device"));
        ASSERT_EQ(result->drivers[0].flags(), fuchsia_driver_development::DeviceFlags::kBound);
      });

  loop.RunUntilIdle();
  ASSERT_TRUE(was_called);
}

TEST_F(DriverDevelopmentTest, DriverInfo) {
  Driver driver;
  driver.name = "test";
  std::vector<const Driver*> drivers;
  driver.bytecode_version = 2;
  auto bind_rules = std::make_unique<uint8_t[]>(1);
  driver.binding = std::move(bind_rules);

  drivers.push_back(&driver);

  auto arena = std::make_unique<fidl::Arena<512>>();
  auto result = GetDriverInfo(*arena, drivers);
  ASSERT_EQ(ZX_OK, result.status_value());

  auto endpoints = fidl::CreateEndpoints<fuchsia_driver_development::DriverInfoIterator>();
  auto iterator = std::make_unique<DriverInfoIterator>(std::move(arena), std::move(*result));

  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), std::move(iterator));

  auto client = fidl::WireClient(std::move(endpoints->client), loop.dispatcher());

  bool was_called = false;
  client->GetNext().Then(
      [&was_called](
          fidl::WireUnownedResult<::fuchsia_driver_development::DriverInfoIterator::GetNext>&
              result) {
        was_called = true;
        auto count = result->drivers.count();
        ASSERT_EQ(count, 1);

        ASSERT_EQ(std::string(result->drivers[0].name().get()), std::string("test"));
      });

  loop.RunUntilIdle();
  ASSERT_TRUE(was_called);
}

TEST_F(DriverDevelopmentTest, UnknownFlagsWork) {
  std::vector<fbl::RefPtr<Device>> devices;
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  devices.push_back(device(parent_index)->device);

  // Give our device some unknown flags.
  devices[0]->flags = 0xF000;
  auto arena = std::make_unique<fidl::Arena<512>>();
  auto result = GetDeviceInfo(*arena, devices);
  ASSERT_EQ(ZX_OK, result.status_value());

  auto endpoints = fidl::CreateEndpoints<fuchsia_driver_development::DeviceInfoIterator>();
  auto iterator = std::make_unique<DeviceInfoIterator>(std::move(arena), std::move(*result));

  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), std::move(iterator));

  auto client = fidl::WireClient(std::move(endpoints->client), loop.dispatcher());

  bool was_called = false;
  client->GetNext().Then(
      [&was_called](
          fidl::WireUnownedResult<::fuchsia_driver_development::DeviceInfoIterator::GetNext>&
              result) {
        was_called = true;
        auto count = result->drivers.count();
        ASSERT_EQ(count, 1);
        ASSERT_EQ(static_cast<uint32_t>(result->drivers[0].flags()), 0);
      });

  loop.RunUntilIdle();
  ASSERT_TRUE(was_called);
}
