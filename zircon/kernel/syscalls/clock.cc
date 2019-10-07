// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "priv.h"

zx_status_t sys_clock_create(uint64_t options, user_in_ptr<const void> user_args,
                             user_out_handle* clock_out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_clock_read(zx_handle_t handle, user_out_ptr<zx_time_t> now) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_clock_get_details(zx_handle_t clock_handle, uint64_t options,
                                  user_out_ptr<void> user_details) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_clock_update(zx_handle_t clock_handle, uint64_t options,
                             user_in_ptr<const void> user_args) {
  return ZX_ERR_NOT_SUPPORTED;
}
