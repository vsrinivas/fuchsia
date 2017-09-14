// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/device.h>
#include <zircon/device/test.h>
#include <zx/time.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>

const char kDevTest[] = "/dev/misc/test";
const char kWlan[] = "wlan";
const char kWlanDriverName[] = "/system/driver/wlan-testdev.so";

int usage(const char* appname) {
    std::cerr << "usage: " << appname << " <add|rm>" << std::endl;
    return 0;
}

int add_wlan() {
    int fd = open(kDevTest, O_RDWR);
    if (fd < 0) {
        std::cerr << "could not open " << kDevTest << ": " << errno << std::endl;
        return -1;
    }

    char devpath[1024];
    ssize_t rc = ioctl_test_create_device(fd, kWlan, std::strlen(kWlan) + 1, devpath, sizeof(devpath));
    if (rc < 0) {
        std::cerr << "could not create test device " << kWlan << ": " << rc << std::endl;
        close(fd);
        return -1;
    }
    std::cerr << "created test device at " << devpath << std::endl;

    int devfd;
    int retry = 0;
    do {
        devfd = open(devpath, O_RDWR);
        if (devfd >= 0) {
            break;
        }
        zx::nanosleep(zx_deadline_after(ZX_SEC(1)));
    } while (++retry < 100);

    if (devfd < 0) {
        std::cerr << "could not open " << devpath << ": " << devfd << std::endl;
        close(fd);
        return -1;
    }

    ioctl_device_bind(devfd, kWlanDriverName, std::strlen(kWlanDriverName) + 1);
    close(devfd);
    close(fd);
    return 0;
}

int rm_wlan() {
    auto path = std::string(kDevTest) + "/" + kWlan;
    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        std::cerr << "could not open " << path << ": " << errno << std::endl;
        return -1;
    }

    ioctl_test_destroy_device(fd);
    std::cerr << path << " removed" << std::endl;
    return 0;
}

int main(int argc, char* argv[]) {
    const char* appname = argv[0];
    if (argc < 2) {
        return usage(appname);
    }

    argc--;
    argv++;
    if (!std::strcmp("add", argv[0])) {
        return add_wlan();
    } else if (!std::strcmp("rm", argv[0])) {
        return rm_wlan();
    } else {
        return usage(appname);
    }
}
