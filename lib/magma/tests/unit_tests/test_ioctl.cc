// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "magma_util/platform/zircon/zircon_platform_ioctl.h"

#include <fcntl.h>

#include <zircon/device/display.h>
#include <fdio/io.h>

constexpr char kDisplayPath[] = "/dev/class/display/000";

TEST(MagmaIoctl, DISPLAY_GET_FB)
{
    int fd = open(kDisplayPath, O_RDWR);
    ASSERT_GE(fd, 0);
    ioctl_display_get_fb_t description;
    ssize_t result = ioctl_display_get_fb(fd, &description);
    ASSERT_EQ(result, static_cast<ssize_t>(sizeof(description)));
    EXPECT_NE(ZX_HANDLE_INVALID, description.vmo);
    EXPECT_GE(description.info.width, 0u);
    EXPECT_GE(description.info.height, 0u);

    close(fd);
}
