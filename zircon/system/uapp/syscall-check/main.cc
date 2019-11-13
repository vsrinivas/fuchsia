// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/syscalls.h>

int main(int argc, char** argv) {
  printf("--- syscall-check ---\n");

#define SYSCALL_STATUS(syscall, default_args)         \
  printf(#syscall ": ");                              \
  if (syscall default_args == ZX_ERR_NOT_SUPPORTED) { \
    printf("disabled\n");                             \
  } else {                                            \
    printf("enabled\n");                              \
  }

  // TODO(scottmg): Add an output backend to kazoo to emit a full list of stub
  // calls to include here.
  size_t actual;
  SYSCALL_STATUS(zx_debug_send_command, (ZX_HANDLE_INVALID, nullptr, 0));
  SYSCALL_STATUS(zx_ktrace_control, (ZX_HANDLE_INVALID, 0, 0, nullptr));
  SYSCALL_STATUS(zx_ktrace_read, (ZX_HANDLE_INVALID, nullptr, 0, 0, &actual));
  SYSCALL_STATUS(zx_ktrace_write, (ZX_HANDLE_INVALID, 0, 0, 0));
  SYSCALL_STATUS(zx_mtrace_control, (ZX_HANDLE_INVALID, 0, 0, 0, nullptr, 0));
  SYSCALL_STATUS(zx_process_write_memory, (ZX_HANDLE_INVALID, 0, nullptr, 0, &actual));
  SYSCALL_STATUS(zx_system_mexec, (ZX_HANDLE_INVALID, ZX_HANDLE_INVALID, ZX_HANDLE_INVALID));
  SYSCALL_STATUS(zx_system_mexec_payload_get, (ZX_HANDLE_INVALID, nullptr, 0));

#undef SYSCALL_STATUS

  return 0;
}
