// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/service/llcpp/service.h>

#include <gtest/gtest.h>

TEST(DeviceWatcherTest, WatchUSBDevice) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto svc = service::OpenServiceRoot();
  ASSERT_TRUE(svc.is_ok());

  auto client_end = service::ConnectAt<fuchsia_device_manager::DeviceWatcher>(
      *svc, "fuchsia.hardware.usb.DeviceWatcher");
  ASSERT_TRUE(client_end.is_ok());

  auto client = fidl::WireClient(std::move(*client_end), loop.dispatcher());
  auto response = client.sync()->NextDevice();
  ASSERT_EQ(response.status(), ZX_OK);

  using NextDevice = fuchsia_device_manager::DeviceWatcher::NextDevice;
  // This response should never return because we already got the single device.
  client->NextDevice().ThenExactlyOnce([](fidl::WireUnownedResult<NextDevice>& result) {
    ASSERT_EQ(result.status(), ZX_ERR_CANCELED);
    ASSERT_EQ(result.reason(), fidl::Reason::kUnbind);
  });

  // This response should return an error because response two is already waiting.
  client->NextDevice().ThenExactlyOnce([&loop](fidl::WireUnownedResult<NextDevice>& result) {
    ASSERT_EQ(result.status(), ZX_OK);
    auto* response = result.Unwrap_NEW();
    ASSERT_TRUE(response->is_error());
    ASSERT_EQ(response->error_value(), ZX_ERR_ALREADY_BOUND);
    loop.Quit();
  });

  loop.Run();
}
