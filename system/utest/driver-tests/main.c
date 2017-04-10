// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <magenta/device/device.h>
#include <magenta/device/test.h>

static const char* test_drivers[] = {
    "iotxn-test",
};

#define DEV_TEST "/dev/misc/test"

static void do_one_test(int tfd, int i) {
    char devpath[1024];
    ssize_t rc = ioctl_test_create_device(tfd, test_drivers[i], strlen(test_drivers[i]) + 1, devpath, sizeof(devpath));
    if (rc < 0) {
        printf("error %zd creating device for %s\n", rc, test_drivers[i]);
    }
    printf("created device for %s at %s\n", test_drivers[i], devpath);

    // TODO some waiting needed before opening..,
    usleep(1000);

    int fd = open(devpath, O_RDWR);
    if (fd < 0) {
        printf("error %d opening device at %s\n", errno, devpath);
    }
    ioctl_device_bind(fd, test_drivers[i], strlen(test_drivers[i]) + 1);

    // TODO some waiting needed before we can run tests...
    usleep(1000);

    test_ioctl_test_report_t report;
    ioctl_test_run_tests(fd, NULL, 0, &report);

    ioctl_test_destroy_device(fd);
    close(fd);
}

int main(int argc, char** argv) {
    int fd = open(DEV_TEST, O_RDWR);
    if (fd < 0) {
        printf("driver-tests: no %s device found\n", DEV_TEST);
        return -1;
    }
    // bind test drivers
    for (unsigned i = 0; i < sizeof(test_drivers)/sizeof(char*); i++) {
        do_one_test(fd, i);
    }
    close(fd);
    return 0;
}
