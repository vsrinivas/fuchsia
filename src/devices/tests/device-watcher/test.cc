// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/service/llcpp/service.h>

#include <gtest/gtest.h>

TEST(DeviceWatcherTest, DISABLED_WatchUSBDevice) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto svc = service::OpenServiceRoot();
  ASSERT_TRUE(svc.is_ok());

  auto client_end = service::ConnectAt<fuchsia_device_manager::DeviceWatcher>(
      *svc, "fuchsia.hardware.usb.DeviceWatcher");
  ASSERT_TRUE(client_end.is_ok());

  auto client = fidl::WireClient(std::move(*client_end), loop.dispatcher());
  auto response = client->NextDevice_Sync();
  ASSERT_EQ(response.status(), ZX_OK);

  // This response should never return because we already got the single device.
  auto response_two = client->NextDevice([](auto response) { ASSERT_TRUE(false); });
  ASSERT_EQ(response_two.status(), ZX_OK);

  // This response should return an error because response two is already waiting.
  auto response_three = client->NextDevice([&loop](auto response) {
    ASSERT_TRUE(response->result.is_err());
    ASSERT_EQ(response->result.err(), ZX_ERR_ALREADY_BOUND);
    loop.Quit();
  });
  ASSERT_EQ(response_three.status(), ZX_OK);

  loop.Run();
}
