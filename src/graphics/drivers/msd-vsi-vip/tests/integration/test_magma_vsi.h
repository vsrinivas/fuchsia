// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_TESTS_INTEGRATION_TEST_MAGMA_VSI_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_TESTS_INTEGRATION_TEST_MAGMA_VSI_H_

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/object.h>
#include <limits.h>

#include <gtest/gtest.h>
#include <magma/magma.h>

class MagmaVsi {
 public:
  void DeviceFind() {
    ASSERT_EQ(device_, 0ul);

    DIR* dir = NULL;

    /* Find matching device. */
    dir = opendir(kDevicePath);
    if (dir != NULL) {
      struct dirent* dp;
      while ((dp = readdir(dir)) != NULL) {
        char filename[NAME_MAX] = {};
        snprintf(filename, sizeof(filename), "%s/%s", kDevicePath, dp->d_name);

        zx::channel server_end, client_end;
        zx::channel::create(0, &server_end, &client_end);
        ASSERT_EQ(fdio_service_connect(filename, server_end.release()), ZX_OK);
        ASSERT_EQ(magma_device_import(client_end.release(), &(device_)), MAGMA_STATUS_OK);

        uint64_t device_id = 0;
        ASSERT_EQ(magma_query(device_, MAGMA_QUERY_DEVICE_ID, nullptr, &device_id),
                  MAGMA_STATUS_OK);

        if (device_id == kVersiliconChipID) {
          break;
        }
        DeviceClose();
      }
      closedir(dir);
    }
    ASSERT_NE(device_, 0ul);
  }

  void DeviceClose() {
    ASSERT_NE(device_, 0ul);
    magma_device_release(device_);
    device_ = 0ul;
  }

  void ConnectionCreate() {
    ASSERT_NE(device_, 0ul);
    ASSERT_EQ(connection_, 0u);
    EXPECT_EQ(magma_create_connection2(device_, &(connection_)), MAGMA_STATUS_OK);
    EXPECT_NE(connection_, 0u);
  }

  void ConnectionRelease() {
    ASSERT_NE(connection_, 0u);
    magma_release_connection(connection_);
    connection_ = 0;
  }

  void ContextCreate() {
    ASSERT_NE(connection_, 0u);
    ASSERT_EQ(context_id_, 0u);
    magma_create_context(connection_, &(context_id_));
    EXPECT_NE(context_id_, 0u);
  }

  void ContextRelease() {
    ASSERT_NE(context_id_, 0u);
    magma_release_context(connection_, context_id_);
    context_id_ = 0u;
  }

  inline magma_connection_t& GetConnection() { return connection_; }

  inline uint32_t& GetContextId() { return context_id_; }

 protected:
  static constexpr const char* kDevicePath = "/dev/class/gpu";

  static constexpr const uint32_t kVersiliconChipID = 0x8000;

  magma_device_t device_ = 0ul;
  magma_connection_t connection_ = {};
  uint32_t context_id_ = 0u;
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_TESTS_INTEGRATION_TEST_MAGMA_VSI_H_
