// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/mediacodec/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <zxtest/zxtest.h>

namespace {
class TestDeviceBase {
 public:
  TestDeviceBase() {}

  void InitializeFromFileName(const char* device_name) {
    zx::channel server_endpoint, client_endpoint;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &server_endpoint, &client_endpoint));

    EXPECT_EQ(ZX_OK, fdio_service_connect(device_name, server_endpoint.release()));
    channel_ = std::move(client_endpoint);
  }

  // Get a channel to the parent device, so we can rebind the driver to it. This
  // can require sandbox access to /dev/sys.
  zx::channel GetParentDevice() {
    char path[llcpp::fuchsia::device::MAX_DEVICE_PATH_LEN + 1];
    auto res = llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(channel());

    EXPECT_EQ(ZX_OK, res.status());
    EXPECT_TRUE(res->result.is_response());

    auto& response = res->result.response();
    EXPECT_LE(response.path.size(), llcpp::fuchsia::device::MAX_DEVICE_PATH_LEN);

    memcpy(path, response.path.data(), response.path.size());
    path[response.path.size()] = 0;
    fprintf(stderr, "Device path: %s\n", path);
    // Remove everything after the final slash.
    *strrchr(path, '/') = 0;
    printf("Parent device path: %s\n", path);
    zx::channel local_channel, remote_channel;
    EXPECT_EQ(ZX_OK, zx::channel::create(0u, &local_channel, &remote_channel));

    EXPECT_EQ(ZX_OK, fdio_service_connect(path, remote_channel.release()));
    return local_channel;
  }

  static void UnbindChildren(const zx::channel& parent_device) {
    auto res = llcpp::fuchsia::device::Controller::Call::UnbindChildren(
        zx::unowned_channel(parent_device));
    EXPECT_EQ(ZX_OK, res.status());
    EXPECT_TRUE(res->result.is_response());
  }

  static bool BindDriver(const zx::channel& parent_device, std::string path) {
    // Rebinding the device immediately after unbinding it sometimes causes the new device to be
    // created before the old one is released, which can cause problems since the old device can
    // hold onto interrupts and other resources. Delay recreation to make that less likely.
    // TODO(fxbug.dev/39852): Remove when the driver framework bug is fixed.
    constexpr uint32_t kRecreateDelayMs = 1000;
    zx::nanosleep(zx::deadline_after(zx::msec(kRecreateDelayMs)));

    constexpr uint32_t kMaxRetryCount = 5;
    uint32_t retry_count = 0;
    while (retry_count++ < kMaxRetryCount) {
      // Don't use rebind because we need the recreate delay above. Also, the parent device may have
      // other children that shouldn't be unbound.
      auto res = llcpp::fuchsia::device::Controller::Call::Bind(zx::unowned_channel(parent_device),
                                                                fidl::unowned_str(path));
      EXPECT_EQ(ZX_OK, res.status());
      if (res->result.is_err() && res->result.err() == ZX_ERR_ALREADY_BOUND) {
        zx::nanosleep(zx::deadline_after(zx::msec(10)));
        continue;
      }
      // This only returns true if the test driver finished binding, which means it successfully ran
      // its built-in tests.  Else the test driver won't succeed binding.
      return res->result.is_response();
    }
    fprintf(stderr, "Timed out rebinding driver\n");
    return false;
  }

  void Unbind() {
    auto res = llcpp::fuchsia::device::Controller::Call::ScheduleUnbind(channel());
    fprintf(stderr, "Result: %d\n", res.status());
  }

  zx::unowned_channel channel() { return zx::unowned_channel(channel_); }
  ~TestDeviceBase() {}

 private:
  zx::channel channel_;
};

TEST(TestRunner, RunTests) {
  auto test_device = std::make_unique<TestDeviceBase>();
  test_device->InitializeFromFileName("/dev/aml-video/amlogic_video");
  zx::channel parent_device = test_device->GetParentDevice();
  test_device.reset();

  TestDeviceBase::UnbindChildren(parent_device);

  bool success =
      TestDeviceBase::BindDriver(parent_device, "/system/driver/amlogic_video_decoder_test.so");
  EXPECT_TRUE(success);
  auto test_device2 = std::make_unique<TestDeviceBase>();
  test_device2->InitializeFromFileName("/dev/aml-video/test_amlogic_video");

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_OK(fdio_service_connect("/tmp", remote.release()));

  auto set_output_res =
      llcpp::fuchsia::hardware::mediacodec::Tester::Call::SetOutputDirectoryHandle(
          test_device2->channel(), std::move(local));
  EXPECT_OK(set_output_res.status());
  auto res = llcpp::fuchsia::hardware::mediacodec::Tester::Call::RunTests(test_device2->channel());
  EXPECT_OK(res.status());
  if (res.ok())
    EXPECT_OK(res->result);

  // UnbindChildren seems to block for some reason.
  test_device2->Unbind();
  test_device2.reset();

  // Try to rebind the correct driver.
  TestDeviceBase::BindDriver(parent_device, "/system/driver/amlogic_video_decoder.so");
}
}  // namespace
