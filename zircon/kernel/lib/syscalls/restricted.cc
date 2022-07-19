// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/syscalls/forward.h>
#include <lib/user_copy/user_ptr.h>
#include <platform.h>
#include <stdint.h>
#include <stdlib.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include <kernel/restricted.h>

#define LOCAL_TRACE 0

zx_status_t sys_restricted_enter(uint32_t options, uintptr_t vector_table_ptr, uintptr_t context) {
  LTRACEF("options %#x vector %#" PRIx64 " context %#" PRIx64 "\n", options, vector_table_ptr,
          context);

  // No options defined for the moment.
  if (options != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  return RestrictedEnter(options, vector_table_ptr, context);
}

zx_status_t sys_restricted_write_state(user_in_ptr<const void> data, size_t data_size) {
  LTRACEF("size %zu\n", data_size);

  return RestrictedWriteState(data, data_size);
}

zx_status_t sys_restricted_read_state(user_out_ptr<void> data, size_t data_size) {
  LTRACEF("size %zu\n", data_size);

  return RestrictedReadState(data, data_size);
}
