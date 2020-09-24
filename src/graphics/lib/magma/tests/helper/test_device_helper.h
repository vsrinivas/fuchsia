// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_
#define SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <filesystem>

#include <gtest/gtest.h>

#include "magma.h"

namespace magma {
class TestDeviceBase {
 public:
  explicit TestDeviceBase(std::string device_name) { InitializeFromFileName(device_name.c_str()); }

  explicit TestDeviceBase(uint64_t vendor_id) { InitializeFromVendorId(vendor_id); }

  void InitializeFromFileName(const char* device_name) {
    zx::channel server_endpoint, client_endpoint;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &server_endpoint, &client_endpoint));

    EXPECT_EQ(ZX_OK, fdio_service_connect(device_name, server_endpoint.release()));
    channel_ = zx::unowned_channel(client_endpoint);
    EXPECT_EQ(MAGMA_STATUS_OK, magma_device_import(client_endpoint.release(), &device_));
  }

  void InitializeFromVendorId(uint64_t id) {
    for (auto& p : std::filesystem::directory_iterator("/dev/class/gpu")) {
      InitializeFromFileName(p.path().c_str());
      uint64_t vendor_id;
      magma_status_t magma_status = magma_query2(device_, MAGMA_QUERY_VENDOR_ID, &vendor_id);
      if (magma_status == MAGMA_STATUS_OK && vendor_id == id) {
        return;
      }

      magma_device_release(device_);
      device_ = 0;
    }
  }

  // Get a channel to the parent device, so we can rebind the driver to it. This
  // requires sandbox access to /dev/sys.
  zx::channel GetParentDevice() {
    char path[llcpp::fuchsia::device::MAX_DEVICE_PATH_LEN + 1];
    auto res = llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(channel());

    EXPECT_EQ(ZX_OK, res.status());
    EXPECT_TRUE(res->result.is_response());

    auto& response = res->result.response();
    EXPECT_LE(response.path.size(), llcpp::fuchsia::device::MAX_DEVICE_PATH_LEN);

    memcpy(path, response.path.data(), response.path.size());
    path[response.path.size()] = 0;
    // Remove everything after the final slash.
    *strrchr(path, '/') = 0;
    printf("Parent device path: %s\n", path);
    zx::channel local_channel, remote_channel;
    EXPECT_EQ(ZX_OK, zx::channel::create(0u, &local_channel, &remote_channel));

    EXPECT_EQ(ZX_OK, fdio_service_connect(path, remote_channel.release()));
    return local_channel;
  }

  void ShutdownDevice() {
    auto res = llcpp::fuchsia::device::Controller::Call::ScheduleUnbind(channel());
    EXPECT_EQ(ZX_OK, res.status());
    EXPECT_TRUE(res->result.is_response());
  }

  static void BindDriver(const zx::channel& parent_device, std::string path) {
    // Rebinding the device immediately after unbinding it sometimes causes the new device to be
    // created before the old one is released, which can cause problems since the old device can
    // hold onto interrupts and other resources. Delay recreation to make that less likely.
    // TODO(fxbug.dev/39852): Remove when the driver framework bug is fixed.
    constexpr uint32_t kRecreateDelayMs = 1000;
    zx::nanosleep(zx::deadline_after(zx::msec(kRecreateDelayMs)));

    constexpr uint32_t kMaxRetryCount = 5000;
    uint32_t retry_count = 0;
    while (true) {
      ASSERT_TRUE(retry_count++ < kMaxRetryCount) << "Timed out rebinding driver";
      // Don't use rebind because we need the recreate delay above. Also, the parent device may have
      // other children that shouldn't be unbound.
      auto res = llcpp::fuchsia::device::Controller::Call::Bind(zx::unowned_channel(parent_device),
                                                                fidl::unowned_str(path));
      ASSERT_EQ(ZX_OK, res.status());
      if (res->result.is_err() && res->result.err() == ZX_ERR_ALREADY_BOUND) {
        zx::nanosleep(zx::deadline_after(zx::msec(10)));
        continue;
      }
      EXPECT_TRUE(res->result.is_response());
      break;
    }
  }

  zx::unowned_channel channel() { return zx::unowned_channel(channel_); }
  magma_device_t device() const { return device_; }
  ~TestDeviceBase() {
    if (device_)
      magma_device_release(device_);
  }

 private:
  magma_device_t device_ = 0;
  zx::unowned_channel channel_;
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_
