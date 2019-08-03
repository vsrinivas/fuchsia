// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>  // for close

#include "magma.h"
#include "test_magma_abi.h"

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

bool test_magma_abi_from_c(const char* device_name) {
  int fd = open(device_name, O_RDONLY);
  if (fd < 0)
    return DRETF(false, "open returned %d", fd);

  uint64_t device_id = 0;
  magma_status_t status = magma_query(fd, MAGMA_QUERY_DEVICE_ID, &device_id);
  if (status != MAGMA_STATUS_OK)
    return DRETF(false, "magma_query return %d", status);

  if (device_id == 0)
    return DRETF(false, "device_id is 0");

  magma_connection_t connection;
  status = magma_create_connection(fd, &connection);
  if (status != MAGMA_STATUS_OK)
    return DRETF(false, "magma_create_connection failed: %d", status);

  magma_release_connection(connection);
  close(fd);

  return true;
}
