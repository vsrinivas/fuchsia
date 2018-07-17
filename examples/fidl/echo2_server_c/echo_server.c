// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/util.h>
#include <lib/async-loop/loop.h>
#include <lib/svc/dir.h>
#include <stdio.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

static void connect(void* context, const char* service_name, zx_handle_t service_request) {
  printf("Incoming connection for %s.\n", service_name);
  // TODO(abarth): Implement echo server once FIDL C bindings are available.
  zx_handle_close(service_request);
}

int main(int argc, char** argv) {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  if (directory_request == ZX_HANDLE_INVALID) {
    printf("error: directory_request was ZX_HANDLE_INVALID\n");
    return -1;
  }

  async_loop_t* loop = NULL;
  zx_status_t status = async_loop_create(&kAsyncLoopConfigAttachToThread, &loop);
  if (status != ZX_OK) {
    printf("error: async_loop_create returned: %d (%s)\n", status, zx_status_get_string(status));
    return status;
  }

  async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);

  svc_dir_t* dir = NULL;
  status = svc_dir_create(dispatcher, directory_request, &dir);
  if (status != ZX_OK) {
    printf("error: svc_dir_create returned: %d (%s)\n", status, zx_status_get_string(status));
    return status;
  }

  status = svc_dir_add_service(dir, "public", "echo2.Echo", NULL, connect);
  if (status != ZX_OK) {
    printf("error: svc_dir_add_service returned: %d (%s)\n", status, zx_status_get_string(status));
    return status;
  }

  status = async_loop_run(loop, ZX_TIME_INFINITE, false);
  if (status != ZX_OK) {
    printf("error: async_loop_run returned: %d (%s)\n", status, zx_status_get_string(status));
    return status;
  }

  svc_dir_destroy(dir);
  async_loop_destroy(loop);

  return 0;
}
