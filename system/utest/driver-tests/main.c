// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <magenta/device/device.h>
#include <magenta/device/test.h>
#include <unittest/unittest.h>

#define DRIVER_TEST_DIR "/boot/driver/test"
#define DEV_TEST "/dev/misc/test"

static void do_one_test(int tfd, const char* drv_libname, mx_handle_t output, test_ioctl_test_report_t* report) {
    char devpath[1024];
    ssize_t rc = ioctl_test_create_device(tfd, drv_libname, strlen(drv_libname) + 1, devpath, sizeof(devpath));
    if (rc < 0) {
        printf("driver-tests: error %zd creating device for %s\n", rc, drv_libname);
        report->n_tests = 1;
        report->n_failed = 1;
        return;
    }

    // TODO some waiting needed before opening..,
    usleep(1000);

    int fd;
    int retry = 0;
    do {
        fd = open(devpath, O_RDWR);
        if (fd >= 0) {
            break;
        }
        usleep(1000);
    } while (++retry < 100);

    if (retry == 100) {
        printf("driver-tests: failed to open %s\n", devpath);
        report->n_tests = 1;
        report->n_failed = 1;
        goto end_device_created;
    }

    char libpath[PATH_MAX];
    int n = snprintf(libpath, sizeof(libpath), "%s/%s", DRIVER_TEST_DIR, drv_libname);
    rc = ioctl_device_bind(fd, libpath, n);
    if (rc < 0) {
        printf("driver-tests: error %zd binding to %s\n", rc, libpath);
        report->n_tests = 1;
        report->n_failed = 1;
        goto end_device_opened;
    }

    mx_handle_t h;
    mx_status_t status = mx_handle_duplicate(output, MX_RIGHT_SAME_RIGHTS, &h);
    if (status != MX_OK) {
        printf("driver-tests: error %d duplicating output socket\n", status);
        report->n_tests = 1;
        report->n_failed = 1;
        goto end_device_opened;
    }

    ioctl_test_set_output_socket(fd, &h);

    rc = ioctl_test_run_tests(fd, NULL, 0, report);
    if (rc < 0) {
        printf("driver-tests: error %zd running tests\n", rc);
        report->n_tests = 1;
        report->n_failed = 1;
    }

end_device_created:
    ioctl_test_destroy_device(fd);
end_device_opened:
    close(fd);
}

static int output_thread(void* arg) {
    mx_handle_t h = *(mx_handle_t*)arg;
    char buf[1024];
    for (;;) {
        mx_status_t status = mx_object_wait_one(h, MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED, MX_TIME_INFINITE, NULL);
        if (status != MX_OK) {
            break;
        }
        size_t bytes = 0;
        status = mx_socket_read(h, 0u, buf, sizeof(buf), &bytes);
        if (status != MX_OK) {
            break;
        }
        size_t written = 0;
        while (written < bytes) {
            ssize_t rc = write(2, buf + written, bytes - written);
            if (rc < 0) {
                break;
            }
            written += rc;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    mx_handle_t socket[2];
    mx_status_t status = mx_socket_create(0u, socket, socket + 1);
    if (status != MX_OK) {
        printf("driver-tests: error creating socket\n");
        return -1;
    }

    int fd = open(DEV_TEST, O_RDWR);
    if (fd < 0) {
        printf("driver-tests: no %s device found\n", DEV_TEST);
        return -1;
    }

    thrd_t t;
    int rc = thrd_create_with_name(&t, output_thread, socket, "driver-test-output");
    if (rc != thrd_success) {
        printf("driver-tests: error %d creating output thread\n", rc);
        close(fd);
        mx_handle_close(socket[0]);
        mx_handle_close(socket[1]);
        return -1;
    }

    test_ioctl_test_report_t final_report;
    memset(&final_report, 0, sizeof(final_report));

    DIR* dir = opendir(DRIVER_TEST_DIR);
    if (dir == NULL) {
        printf("driver-tests: failed to open %s\n", DRIVER_TEST_DIR);
        return -1;
    }
    int dfd = dirfd(dir);
    if (dfd < 0) {
        printf("driver-tests: failed to get fd for %s\n", DRIVER_TEST_DIR);
        return -1;
    }
    struct dirent* de;
    // bind test drivers
    while ((de = readdir(dir)) != NULL) {
        if ((strcmp(de->d_name, ".") == 0) || (strcmp(de->d_name, "..") == 0)) {
            continue;
        }
        test_ioctl_test_report_t one_report;
        memset(&one_report, 0, sizeof(one_report));
        do_one_test(fd, de->d_name, socket[1], &one_report);
        final_report.n_tests += one_report.n_tests;
        final_report.n_success += one_report.n_success;
        final_report.n_failed += one_report.n_failed;
    }
    close(fd);

    // close this handle before thrd_join to get PEER_CLOSED in output thread
    mx_handle_close(socket[1]);

    thrd_join(t, NULL);
    mx_handle_close(socket[0]);

    unittest_printf_critical(
            "\n====================================================\n");
    unittest_printf_critical(
            "    CASES:  %d     SUCCESS:  %d     FAILED:  %d   ",
            final_report.n_tests, final_report.n_success, final_report.n_failed);
    unittest_printf_critical(
            "\n====================================================\n");

    return final_report.n_failed == 0 ? 0 : -1;
}
