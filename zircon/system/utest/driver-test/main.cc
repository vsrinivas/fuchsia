// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/test/llcpp/fidl.h>
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

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>

static constexpr const char kDriverTestDir[] = "/boot/driver/test";
static constexpr const char kBindFailDriver[] = "bind-fail-test.so";

using devmgr_integration_test::IsolatedDevmgr;

namespace {

void do_one_test(const IsolatedDevmgr& devmgr,
                 ::llcpp::fuchsia::device::test::RootDevice::SyncClient* test_root,
                 const char* drv_libname, const zx::socket& output,
                 ::llcpp::fuchsia::device::test::TestReport* report) {
  // Initialize the report with a failure state to handle early returns
  *report = {
      .test_count = 1,
      .success_count = 0,
      .failure_count = 1,
  };

  zx::channel test_channel, test_remote;
  zx_status_t status = zx::channel::create(0, &test_channel, &test_remote);
  if (status != ZX_OK) {
    printf("driver-tests: failed to create channel %s\n", zx_status_get_string(status));
    return;
  }

  auto result = test_root->CreateDevice(fidl::unowned_str(drv_libname, strlen(drv_libname)),
                                        std::move(test_remote));
  if (result.status() != ZX_OK) {
    printf("driver-tests: error %s during IPC for creating device for %s\n",
           zx_status_get_string(result.status()), drv_libname);
    return;
  }
  if (result->status != ZX_OK) {
    printf("driver-tests: error %s creating device for %s\n", zx_status_get_string(result->status),
           drv_libname);
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
    // TODO(teisenbe): I think ::llcpp::fuchsia::device::test::DeviceDestroy() should be called
    // here?
    return;
  }

  // Check that Bind was synchronous by looking for the child device.
  fbl::StringBuffer<::llcpp::fuchsia::device::test::MAX_DEVICE_PATH_LEN + 1> child_devpath;
  {
    const char* kDevPrefix = "/dev/";
    if (result->path.size() < strlen(kDevPrefix) ||
        memcmp(result->path.data(), kDevPrefix, strlen(kDevPrefix))) {
      printf("driver-tests: bad path when creating device for %s: %.*s\n", drv_libname,
             static_cast<int>(result->path.size()), result->path.data());
      return;
    }

    child_devpath.Append(result->path.data() + strlen(kDevPrefix),
                         result->path.size() - strlen(kDevPrefix));
    child_devpath.Append("/child");
  }
  fbl::unique_fd fd(openat(devmgr.devfs_root().get(), child_devpath.data(), O_RDWR));
  if (!fd.is_valid()) {
    printf("driver-tests: error binding device %s\n", child_devpath.data());
    return;
  }

  zx::socket output_copy;
  status = output.duplicate(ZX_RIGHT_SAME_RIGHTS, &output_copy);
  if (status != ZX_OK) {
    printf("driver-tests: error %d duplicating output socket\n", status);
    // TODO(teisenbe): I think ::llcpp::fuchsia::device::test::DeviceDestroy() should be called
    // here?
    return;
  }

  ::llcpp::fuchsia::device::test::Device::SyncClient test_client{std::move(test_channel)};

  test_client.SetOutputSocket(std::move(output_copy));
  auto test_result = test_client.RunTests();
  if (test_result.status() != ZX_OK || test_result->status != ZX_OK) {
    zx_status_t status =
        (test_result.status() != ZX_OK) ? test_result.status() : test_result->status;
    printf("driver-tests: error %s running tests\n", zx_status_get_string(status));
    *report = {
        .test_count = 1,
        .success_count = 0,
        .failure_count = 1,
    };
  } else {
    *report = test_result->report;
  }

  test_client.Destroy();
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

  ::llcpp::fuchsia::device::test::RootDevice::SyncClient test_root{zx::channel{}};
  {
    zx::channel test_root_channel;
    status = fdio_get_service_handle(fd.release(), test_root_channel.reset_and_get_address());
    if (status != ZX_OK) {
      printf("driver-tests: failed to get root channel %s\n", zx_status_get_string(status));
      return -1;
    }
    *test_root.mutable_channel() = std::move(test_root_channel);
  }

  thrd_t t;
  int rc = thrd_create_with_name(&t, output_thread, reinterpret_cast<void*>(local_socket.release()),
                                 "driver-test-output");
  if (rc != thrd_success) {
    printf("driver-tests: error %d creating output thread\n", rc);
    return -1;
  }

  ::llcpp::fuchsia::device::test::TestReport final_report = {};

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
        strcmp(de->d_name, "unit-test-pass.so") == 0 || strcmp(de->d_name, "mock-device.so") == 0 ||
        strcmp(de->d_name, "bind-debugger-test.so") == 0) {
      continue;
    }
    ::llcpp::fuchsia::device::test::TestReport one_report = {};
    do_one_test(devmgr, &test_root, de->d_name, remote_socket, &one_report);

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
