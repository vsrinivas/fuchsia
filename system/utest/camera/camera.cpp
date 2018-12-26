// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <stdio.h>
#include <unistd.h>
#include <unittest/unittest.h>
#include <zircon/device/camera.h>

namespace {

const char kCameraDir[] = "/dev/class/camera";

zx_status_t StartCameraTest(int* fd) {
    struct dirent* de;
    DIR* dir = opendir(kCameraDir);
    if (!dir) {
        printf("Error opening %s\n", kCameraDir);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        char devname[128];

        snprintf(devname, sizeof(devname), "%s/%s", kCameraDir, de->d_name);
        printf("\nOpening %s\n", devname);
        *fd = open(devname, O_RDWR);
        if (*fd < 0) {
            printf("Error opening device%s\n", devname);
            return -1;
        }

        closedir(dir);
        return ZX_OK;
    }
    closedir(dir);
    return -1;
}

bool TestSupportedModes(void) {
    BEGIN_TEST;

    int fd;
    // Open the camera sensor.
    ASSERT_EQ(StartCameraTest(&fd), ZX_OK, "");

    // Get the modes supported.
    auto* supported_modes = new fuchsia_hardware_camera_SensorMode[MAX_SUPPORTED_MODES];
    ASSERT_NE(supported_modes, nullptr, "");

    ssize_t rc = ioctl_camera_get_supported_modes(
        fd, supported_modes, sizeof(fuchsia_hardware_camera_SensorMode) * MAX_SUPPORTED_MODES);
    ASSERT_GE(rc, 0, "");

    close(fd);
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(camera_tests)
RUN_TEST(TestSupportedModes)
END_TEST_CASE(camera_tests)

int main(int argc, char* argv[]) {
    // Disabling since this test is intended
    // to be tested on HW manually.
    return 0;

    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
