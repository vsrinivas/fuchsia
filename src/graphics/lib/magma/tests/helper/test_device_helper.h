// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_
#define SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>

#include <filesystem>

#include <gtest/gtest.h>

#include "magma/magma.h"

namespace magma {
class TestDeviceBase {
 public:
  explicit TestDeviceBase(std::string device_name) { InitializeFromFileName(device_name.c_str()); }

  explicit TestDeviceBase(uint64_t vendor_id) { InitializeFromVendorId(vendor_id); }

  TestDeviceBase() = default;

  void InitializeFromFileName(const char* device_name) {
    auto client = component::Connect<fuchsia_device::Controller>(device_name);
    ASSERT_TRUE(client.is_ok());
    device_controller_ = client->borrow();
    EXPECT_EQ(MAGMA_STATUS_OK, magma_device_import(client->TakeChannel().release(), &device_));
  }

  void InitializeFromVendorId(uint64_t id) {
    for (auto& p : std::filesystem::directory_iterator("/dev/class/gpu")) {
      InitializeFromFileName(p.path().c_str());
      uint64_t vendor_id;
      magma_status_t magma_status = magma_query(device_, MAGMA_QUERY_VENDOR_ID, NULL, &vendor_id);
      if (magma_status == MAGMA_STATUS_OK && vendor_id == id) {
        return;
      }

      magma_device_release(device_);
      device_ = 0;
    }
    GTEST_FAIL();
  }

  // Get a channel to the parent device, so we can rebind the driver to it. This
  // requires sandbox access to /dev/sys.
  fidl::ClientEnd<fuchsia_device::Controller> GetParentDevice() {
    char path[fuchsia_device::wire::kMaxDevicePathLen + 1];
    auto res = fidl::WireCall<fuchsia_device::Controller>(device_controller_)->GetTopologicalPath();

    EXPECT_EQ(ZX_OK, res.status());
    EXPECT_TRUE(res->is_ok());

    auto& response = *res->value();
    EXPECT_LE(response.path.size(), fuchsia_device::wire::kMaxDevicePathLen);

    memcpy(path, response.path.data(), response.path.size());
    path[response.path.size()] = 0;
    // Remove everything after the final slash.
    *strrchr(path, '/') = 0;

    auto parent = component::Connect<fuchsia_device::Controller>(path);

    EXPECT_EQ(ZX_OK, parent.status_value());
    return std::move(*parent);
  }

  void ShutdownDevice() {
    auto res = fidl::WireCall<fuchsia_device::Controller>(device_controller_)->ScheduleUnbind();
    EXPECT_EQ(ZX_OK, res.status());
    EXPECT_TRUE(res->is_ok());
  }

  static void AutobindDriver(fidl::UnownedClientEnd<fuchsia_device::Controller> parent_device) {
    BindDriver(std::move(parent_device), "");
  }

  static void BindDriver(fidl::UnownedClientEnd<fuchsia_device::Controller> parent_device,
                         std::string path) {
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
      auto res = fidl::WireCall<fuchsia_device::Controller>(parent_device)
                     ->Bind(fidl::StringView::FromExternal(path));
      ASSERT_EQ(ZX_OK, res.status());
      if (res->is_error() && res->error_value() == ZX_ERR_ALREADY_BOUND) {
        zx::nanosleep(zx::deadline_after(zx::msec(10)));
        continue;
      }
      EXPECT_TRUE(res->is_ok());
      break;
    }
  }

  zx::unowned_channel channel() { return zx::unowned_channel(device_controller_.channel()); }
  magma_device_t device() const { return device_; }
  ~TestDeviceBase() {
    if (device_)
      magma_device_release(device_);
  }

 private:
  magma_device_t device_ = 0;
  fidl::UnownedClientEnd<fuchsia_device::Controller> device_controller_{ZX_HANDLE_INVALID};
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_
