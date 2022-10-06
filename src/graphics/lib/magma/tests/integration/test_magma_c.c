// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>  // for close
#if defined(__Fuchsia__)
#include <lib/fdio/directory.h>
#include <zircon/syscalls.h>
#endif

#include "magma/magma.h"
#include "test_magma.h"

__attribute__((format(printf, 3, 4))) static inline bool printf_return_false(const char* file,
                                                                             int line,
                                                                             const char* msg, ...) {
  printf("%s:%d returning false: ", file, line);
  va_list args;
  va_start(args, msg);
  vprintf(msg, args);
  va_end(args);
  printf("\n");
  return false;
}

#define DRETF(ret, ...) (ret ? true : printf_return_false(__FILE__, __LINE__, __VA_ARGS__))

bool test_magma_from_c(const char* device_name) {
#if defined(__Fuchsia__)
  zx_handle_t client_end, server_end;
  zx_channel_create(0, &client_end, &server_end);

  zx_status_t zx_status = fdio_service_connect(device_name, server_end);
  if (zx_status != ZX_OK)
    return DRETF(false, "fdio_service_connect return %d", zx_status);

  magma_device_t device;
  magma_status_t status = magma_device_import(client_end, &device);
  if (status != MAGMA_STATUS_OK)
    return DRETF(false, "magma_device_import return %d", status);
#else
  int fd = open(device_name, O_RDWR);
  if (fd < 0)
    return DRETF(false, "open returned %d", fd);

  magma_device_t device;
  magma_status_t status = magma_device_import(fd, &device);
  if (status != MAGMA_STATUS_OK)
    return DRETF(false, "magma_device_import return %d", status);
#endif

  uint64_t device_id = 0;
  status = magma_query(device, MAGMA_QUERY_DEVICE_ID, NULL, &device_id);
  if (status != MAGMA_STATUS_OK)
    return DRETF(false, "magma_query return %d", status);

  if (device_id == 0)
    return DRETF(false, "device_id is 0");

  magma_connection_t connection;
  status = magma_create_connection2(device, &connection);
  if (status != MAGMA_STATUS_OK)
    return DRETF(false, "magma_create_connection failed: %d", status);

  magma_release_connection(connection);
  magma_device_release(device);

  return true;
}
