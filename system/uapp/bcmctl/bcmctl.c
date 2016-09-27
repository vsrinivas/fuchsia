// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/util.h>
#include <ddk/protocol/bcm.h>

const char* prog_name;

void print_usage(void) {
    printf("Usage:\n");
    printf("\n");
    printf("%s usbon\n", prog_name);
    printf("usbon: Power on the USB DWC device.\n");
}

int usb_pwr(int fd) {
    ssize_t ret = ioctl_bcm_power_on_usb(fd);
    if (ret < 0) {
        printf("Error while enabling USB device. ret = %zd\n", ret);
        return 1;
    }

    return 0;
}

int main(int argc, const char** argv) {
    if (argc < 1)
        return 1;

    prog_name = argv[0];

    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char* cmd = argv[1];

    int fd = open("/dev/misc/bcm-vc-rpc", O_RDWR);
    if (fd < 0) {
        printf("Error opening bcm mailbox device.\n");
        return 1;
    }

    if (!strcmp("usbon", cmd)) {
        return usb_pwr(fd);
    } else {
        printf("Unrecognized command %s.\n", cmd);
        print_usage();
        return 1;
    }

    printf("We should never get here!.\n");
    return 1;
}
