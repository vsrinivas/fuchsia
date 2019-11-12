// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/test/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <lib/zx/time.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>

static constexpr const char kDriverTestDir[] = "/boot/driver/test";
static constexpr const char kBindFailDriver[] = "bind-fail-test.so";

using devmgr_integration_test::IsolatedDevmgr;

namespace {

void do_one_test(const IsolatedDevmgr& devmgr, const zx::channel& test_root,
                 const char* drv_libname, const zx::socket& output,
                 fuchsia_device_test_TestReport* report) {
  // Initialize the report with a failure state to handle early returns
  *report = {
      .test_count = 1,
      .success_count = 0,
      .failure_count = 1,
  };

  char devpath[fuchsia_device_test_MAX_DEVICE_PATH_LEN + 1];
  size_t devpath_count;
  zx_status_t call_status;
  zx_status_t status = fuchsia_device_test_RootDeviceCreateDevice(
      test_root.get(), drv_libname, strlen(drv_libname), &call_status, devpath, sizeof(devpath) - 1,
      &devpath_count);
  if (status == ZX_OK) {
    status = call_status;
  }
  if (status != ZX_OK) {
    printf("driver-tests: error %s creating device for %s\n", zx_status_get_string(status),
           drv_libname);
    return;
  }
  devpath[devpath_count] = 0;

  const char* kDevPrefix = "/dev/";
  if (strncmp(devpath, kDevPrefix, strlen(kDevPrefix))) {
    printf("driver-tests: bad path when creating device for %s: %s\n", drv_libname, devpath);
    return;
  }

  const char* relative_devpath = devpath + strlen(kDevPrefix);

  fbl::unique_fd fd;
  status =
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), relative_devpath, &fd);
  if (status != ZX_OK) {
    printf("driver-tests: failed to open %s\n", devpath);
    return;
  }

  zx::channel test_channel;
  status = fdio_get_service_handle(fd.release(), test_channel.reset_and_get_address());
  if (status != ZX_OK) {
    printf("driver-tests: failed to get channel %s\n", zx_status_get_string(status));
    return;
  }

  char libpath[PATH_MAX];
  int n = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, drv_libname);
  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(test_channel.get()), ::fidl::StringView(libpath, n));
  status = resp.status();
  if (status == ZX_OK) {
    if (resp->result.is_err()) {
      status = resp->result.err();
    }
  }
  if (status == ZX_ERR_NOT_SUPPORTED &&
      !strncmp(drv_libname, kBindFailDriver, strlen(kBindFailDriver))) {
    *report = {
        .test_count = 1,
        .success_count = 1,
        .failure_count = 0,
    };
    return;
  }
  if (status != ZX_OK) {
    printf("driver-tests: error %d binding to %s\n", status, libpath);
    // TODO(teisenbe): I think fuchsia_device_test_DeviceDestroy() should be called
    // here?
    return;
  }

  // Check that Bind was synchronous by looking for the child device.
  char child_devpath[fuchsia_device_test_MAX_DEVICE_PATH_LEN + 1];
  snprintf(child_devpath, sizeof(child_devpath), "%s/child", relative_devpath);
  fd.reset(openat(devmgr.devfs_root().get(), child_devpath, O_RDWR));
  if (!fd.is_valid()) {
    printf("driver-tests: error binding device %s %s\n", devpath, relative_devpath);
    return;
  }

  zx::socket output_copy;
  status = output.duplicate(ZX_RIGHT_SAME_RIGHTS, &output_copy);
  if (status != ZX_OK) {
    printf("driver-tests: error %d duplicating output socket\n", status);
    // TODO(teisenbe): I think fuchsia_device_test_DeviceDestroy() should be called
    // here?
    return;
  }

  fuchsia_device_test_DeviceSetOutputSocket(test_channel.get(), output_copy.release());

  status = fuchsia_device_test_DeviceRunTests(test_channel.get(), &call_status, report);
  if (status == ZX_OK) {
    status = call_status;
  }
  if (status != ZX_OK) {
    printf("driver-tests: error %s running tests\n", zx_status_get_string(status));
    *report = {
        .test_count = 1,
        .success_count = 0,
        .failure_count = 1,
    };
  }

  fuchsia_device_test_DeviceDestroy(test_channel.get());
}

int output_thread(void* arg) {
  zx::socket h(static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(arg)));
  char buf[1024];
  for (;;) {
    zx_status_t status =
        h.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), NULL);
    if (status != ZX_OK) {
      break;
    }
    size_t bytes = 0;
    status = h.read(0u, buf, sizeof(buf), &bytes);
    if (status != ZX_OK) {
      break;
    }
    size_t written = 0;
    while (written < bytes) {
      ssize_t rc = write(STDERR_FILENO, buf + written, bytes - written);
      if (rc < 0) {
        break;
      }
      written += rc;
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  auto args = IsolatedDevmgr::DefaultArgs();

  IsolatedDevmgr devmgr;
  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  if (status != ZX_OK) {
    printf("driver-tests: failed to create isolated devmgr\n");
    return -1;
  }

  zx::socket local_socket, remote_socket;
  status = zx::socket::create(0u, &local_socket, &remote_socket);
  if (status != ZX_OK) {
    printf("driver-tests: error creating socket\n");
    return -1;
  }

  // Wait for /dev/test/test to appear
  fbl::unique_fd fd;
  status = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd);
  if (status != ZX_OK) {
    printf("driver-tests: failed to find /dev/test/test\n");
    return -1;
  }

  zx::channel test_root;
  status = fdio_get_service_handle(fd.release(), test_root.reset_and_get_address());
  if (status != ZX_OK) {
    printf("driver-tests: failed to get root channel %s\n", zx_status_get_string(status));
    return -1;
  }

  thrd_t t;
  int rc = thrd_create_with_name(&t, output_thread, reinterpret_cast<void*>(local_socket.release()),
                                 "driver-test-output");
  if (rc != thrd_success) {
    printf("driver-tests: error %d creating output thread\n", rc);
    return -1;
  }

  fuchsia_device_test_TestReport final_report = {};

  DIR* dir = opendir(kDriverTestDir);
  if (dir == NULL) {
    printf("driver-tests: failed to open %s\n", kDriverTestDir);
    return -1;
  }
  int dfd = dirfd(dir);
  if (dfd < 0) {
    printf("driver-tests: failed to get fd for %s\n", kDriverTestDir);
    return -1;
  }
  struct dirent* de;
  // bind test drivers
  while ((de = readdir(dir)) != NULL) {
    if ((strcmp(de->d_name, ".") == 0) || (strcmp(de->d_name, "..") == 0)) {
      continue;
    }
    // Don't try to bind the fake sysdev, mock device, or unit test test devices.
    if (strcmp(de->d_name, "sysdev.so") == 0 || strcmp(de->d_name, "fidl-llcpp-driver.so") == 0 ||
        strcmp(de->d_name, "fidl-async-llcpp-driver.so") == 0 ||
        strcmp(de->d_name, "unit-test-fail.so") == 0 ||
        strcmp(de->d_name, "unit-test-pass.so") == 0 || strcmp(de->d_name, "mock-device.so") == 0) {
      continue;
    }
    fuchsia_device_test_TestReport one_report = {};
    do_one_test(devmgr, test_root, de->d_name, remote_socket, &one_report);

    final_report.test_count += one_report.test_count;
    final_report.success_count += one_report.success_count;
    final_report.failure_count += one_report.failure_count;
  }

  // close this handle before thrd_join to get PEER_CLOSED in output thread
  remote_socket.reset();

  thrd_join(t, NULL);

  fprintf(stderr, "\n====================================================\n");
  fprintf(stderr, "    CASES:  %d     SUCCESS:  %d     FAILED:  %d   ", final_report.test_count,
          final_report.success_count, final_report.failure_count);
  fprintf(stderr, "\n====================================================\n");

  return final_report.failure_count == 0 ? 0 : -1;
}
