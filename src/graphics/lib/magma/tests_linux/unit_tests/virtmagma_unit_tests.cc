// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <gtest/gtest.h>

#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"
#include "src/graphics/lib/magma/src/libmagma_linux/virtmagma.h"

class VirtMagmaUnitTest : public ::testing::Test {
 protected:
  VirtMagmaUnitTest() {}

  ~VirtMagmaUnitTest() override {}

  void SetUp() override {
    fd_ = open("/dev/magma0", O_RDWR);
    ASSERT_GE(fd_, 0);
  }

  void TearDown() override { close(fd_); }

  int fd_;
};

// Bypasses libmagma because passing an invalid buffer would cause a client-side crash.
TEST_F(VirtMagmaUnitTest, GetIdForInvalidBuffer) {
  virtio_magma_get_buffer_id_ctrl_t request = {
      .hdr = {.type = VIRTIO_MAGMA_CMD_GET_BUFFER_ID},
      .buffer = 0x12345678abcd1234,
  };
  virtio_magma_get_buffer_id_resp_t response{};

  virtmagma_ioctl_args_magma_command command = {
      .request_address = reinterpret_cast<uintptr_t>(&request),
      .request_size = sizeof(request),
      .response_address = reinterpret_cast<uintptr_t>(&response),
      .response_size = sizeof(response)};

  int ret = ioctl(fd_, VIRTMAGMA_IOCTL_MAGMA_COMMAND, &command);
  if (ret == -1)
    EXPECT_EQ(errno, -EINVAL);
  EXPECT_EQ(response.result_return, 0u);
}
