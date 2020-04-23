// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ERRORS_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ERRORS_H_

#include <acpica/acpi.h>

static inline zx_status_t acpi_to_zx_status(ACPI_STATUS acpi_status) {
  switch (acpi_status) {
    case AE_ERROR:
    case AE_NO_ACPI_TABLES:
      return ZX_ERR_INTERNAL;
    case AE_NO_NAMESPACE:
      return ZX_ERR_NOT_FOUND;
    case AE_NO_MEMORY:
      return ZX_ERR_NO_MEMORY;
    case AE_NOT_EXIST:
      return ZX_ERR_NOT_FOUND;
    case AE_ALREADY_EXISTS:
      return ZX_ERR_ALREADY_EXISTS;
    case AE_TYPE:
      return ZX_ERR_WRONG_TYPE;
    case AE_NULL_OBJECT:
    case AE_NULL_ENTRY:
      return ZX_ERR_NOT_FOUND;
    case AE_BUFFER_OVERFLOW:
      return ZX_ERR_BUFFER_TOO_SMALL;
    case AE_STACK_OVERFLOW:
    case AE_STACK_UNDERFLOW:
      return ZX_ERR_INTERNAL;
    case AE_NOT_IMPLEMENTED:
    case AE_SUPPORT:
      return ZX_ERR_NOT_SUPPORTED;
    case AE_LIMIT:
      return ZX_ERR_INTERNAL;
    case AE_TIME:
      return ZX_ERR_TIMED_OUT;
    case AE_ACQUIRE_DEADLOCK:
    case AE_RELEASE_DEADLOCK:
    case AE_NOT_ACQUIRED:
    case AE_ALREADY_ACQUIRED:
      return ZX_ERR_INTERNAL;
    case AE_NO_HARDWARE_RESPONSE:
      return ZX_ERR_TIMED_OUT;
    case AE_NO_GLOBAL_LOCK:
      return ZX_ERR_INTERNAL;
    case AE_ABORT_METHOD:
      return ZX_ERR_INTERNAL;
    case AE_SAME_HANDLER:
      return ZX_ERR_ALREADY_EXISTS;
    case AE_OWNER_ID_LIMIT:
      return ZX_ERR_NO_RESOURCES;
    case AE_NOT_CONFIGURED:
      return ZX_ERR_NOT_FOUND;
    case AE_ACCESS:
      return ZX_ERR_ACCESS_DENIED;
    case AE_IO_ERROR:
      return ZX_ERR_IO;
    case AE_OK:
      return ZX_OK;
    default:
      return ZX_ERR_INTERNAL;
  }
}

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ERRORS_H_
