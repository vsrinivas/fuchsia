// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h> // for close

#include "magma_util/macros.h"
#include "magma_util/platform/magenta/magenta_platform_ioctl.h"

const char* kDeviceName = "/dev/class/display/000";

int main(int argc, char** argv)
{
    int fd = open(kDeviceName, O_RDONLY);
    if (fd < 0) {
        printf("Failed to open display device: %s\n", kDeviceName);
        return -1;
    }

    int ret = mxio_ioctl(fd, IOCTL_MAGMA_DUMP_STATUS, nullptr, 0, nullptr, 0);
    magma::log(magma::LOG_INFO, "Dumping system driver status to system log (%d)", ret);

    close(fd);
    return 0;
}
