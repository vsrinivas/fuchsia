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

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#include <zircon/device/device.h>
#include <zircon/device/test.h>
#include <unittest/unittest.h>

#define DRIVER_TEST_DIR "/boot/driver/test"

using devmgr_integration_test::IsolatedDevmgr;

namespace {

void do_one_test(const fbl::unique_ptr<IsolatedDevmgr>& devmgr, const fbl::unique_fd& tfd,
                 const char* drv_libname, zx_handle_t output, test_ioctl_test_report_t* report) {
    char devpath[1024];
    ssize_t rc = ioctl_test_create_device(tfd.get(), drv_libname, strlen(drv_libname) + 1, devpath,
                                          sizeof(devpath));
    if (rc < 0) {
        printf("driver-tests: error %zd creating device for %s\n", rc, drv_libname);
        report->n_tests = 1;
        report->n_failed = 1;
        return;
    }

    const char* kDevPrefix = "/dev/";
    if (strncmp(devpath, kDevPrefix, strlen(kDevPrefix))) {
        printf("driver-tests: bad path when creating device for %s: %s\n", drv_libname, devpath);
        report->n_tests = 1;
        report->n_failed = 1;
        return;
    }

    const char* relative_devpath = devpath + strlen(kDevPrefix);

    // TODO some waiting needed before opening..,
    usleep(1000);

    fbl::unique_fd fd;
    int retry = 0;
    do {
        fd.reset(openat(devmgr->devfs_root().get(), relative_devpath, O_RDWR));
        if (fd.is_valid()) {
            break;
        }
        usleep(1000);
    } while (++retry < 100);

    if (retry == 100) {
        printf("driver-tests: failed to open %s\n", devpath);
        report->n_tests = 1;
        report->n_failed = 1;
        ioctl_test_destroy_device(fd.get());
        return;
    }

    char libpath[PATH_MAX];
    int n = snprintf(libpath, sizeof(libpath), "%s/%s", DRIVER_TEST_DIR, drv_libname);
    rc = ioctl_device_bind(fd.get(), libpath, n);
    if (rc < 0) {
        printf("driver-tests: error %zd binding to %s\n", rc, libpath);
        report->n_tests = 1;
        report->n_failed = 1;
        // TODO(teisenbe): I think ioctl_test_destroy_device() should be called
        // here?
        return;
    }

    zx_handle_t h;
    zx_status_t status = zx_handle_duplicate(output, ZX_RIGHT_SAME_RIGHTS, &h);
    if (status != ZX_OK) {
        printf("driver-tests: error %d duplicating output socket\n", status);
        report->n_tests = 1;
        report->n_failed = 1;
        // TODO(teisenbe): I think ioctl_test_destroy_device() should be called
        // here?
        return;
    }

    ioctl_test_set_output_socket(fd.get(), &h);

    rc = ioctl_test_run_tests(fd.get(), report);
    if (rc < 0) {
        printf("driver-tests: error %zd running tests\n", rc);
        report->n_tests = 1;
        report->n_failed = 1;
    }

    ioctl_test_destroy_device(fd.get());
}

int output_thread(void* arg) {
    zx_handle_t h = *(zx_handle_t*)arg;
    char buf[1024];
    for (;;) {
        zx_status_t status = zx_object_wait_one(h, ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, ZX_TIME_INFINITE, NULL);
        if (status != ZX_OK) {
            break;
        }
        size_t bytes = 0;
        status = zx_socket_read(h, 0u, buf, sizeof(buf), &bytes);
        if (status != ZX_OK) {
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

} // namespace

int main(int argc, char** argv) {
    auto args = IsolatedDevmgr::DefaultArgs();

    fbl::unique_ptr<IsolatedDevmgr> devmgr;
    zx_status_t status = IsolatedDevmgr::Create(args, &devmgr);
    if (status != ZX_OK) {
        printf("driver-tests: failed to create isolated devmgr\n");
        return -1;
    }

    zx_handle_t socket[2];
    status = zx_socket_create(0u, socket, socket + 1);
    if (status != ZX_OK) {
        printf("driver-tests: error creating socket\n");
        return -1;
    }

    // Wait for /dev/test/test to appear
    fbl::unique_fd fd;
    status = devmgr_integration_test::RecursiveWaitForFile(devmgr->devfs_root(), "test/test",
                                                           zx::deadline_after(zx::sec(5)), &fd);
    if (status != ZX_OK) {
        printf("driver-tests: failed to find /dev/test/test\n");
        return -1;
    }

    thrd_t t;
    int rc = thrd_create_with_name(&t, output_thread, socket, "driver-test-output");
    if (rc != thrd_success) {
        printf("driver-tests: error %d creating output thread\n", rc);
        zx_handle_close(socket[0]);
        zx_handle_close(socket[1]);
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
        // Don't try to bind the fake sysdev
        if (strcmp(de->d_name, "sysdev.so") == 0) {
            continue;
        }
        test_ioctl_test_report_t one_report;
        memset(&one_report, 0, sizeof(one_report));
        do_one_test(devmgr, fd, de->d_name, socket[1], &one_report);
        final_report.n_tests += one_report.n_tests;
        final_report.n_success += one_report.n_success;
        final_report.n_failed += one_report.n_failed;
    }

    // close this handle before thrd_join to get PEER_CLOSED in output thread
    zx_handle_close(socket[1]);

    thrd_join(t, NULL);
    zx_handle_close(socket[0]);

    unittest_printf_critical(
            "\n====================================================\n");
    unittest_printf_critical(
            "    CASES:  %d     SUCCESS:  %d     FAILED:  %d   ",
            final_report.n_tests, final_report.n_success, final_report.n_failed);
    unittest_printf_critical(
            "\n====================================================\n");

    return final_report.n_failed == 0 ? 0 : -1;
}
