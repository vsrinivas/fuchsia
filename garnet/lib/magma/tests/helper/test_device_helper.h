// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_
#define GARNET_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <filesystem>

#include "gtest/gtest.h"
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

  zx::unowned_channel channel() {
    return zx::unowned_channel(channel_);
  }
  magma_device_t device() const {
    return device_;
  }
  ~TestDeviceBase() {
    if (device_)
      magma_device_release(device_);
  }

 private:
  magma_device_t device_ = 0;
  zx::unowned_channel channel_;
};

}  // namespace magma

#endif  // GARNET_LIB_MAGMA_TESTS_HELPER_TEST_DEVICE_HELPER_H_
