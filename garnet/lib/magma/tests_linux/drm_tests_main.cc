// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <gtest/gtest.h>
#include <helper/platform_device_helper.h>

int main(int argc, char** argv) {
  int fd = open("/dev/dri/renderD128", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "Failed to open gpu device\n");
    return -1;
  }

  void* device = reinterpret_cast<void*>(fd);

  TestPlatformDevice::SetInstance(magma::PlatformDevice::Create(device));
  SetTestDeviceHandle(device);

  testing::InitGoogleTest(&argc, argv);

  int ret = RUN_ALL_TESTS();

  close(fd);

  return ret;
}
