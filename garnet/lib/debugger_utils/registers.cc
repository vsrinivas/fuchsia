// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <src/lib/fxl/logging.h>

#include "util.h"

namespace debugger_utils {

zx_status_t ReadGeneralRegisters(zx_handle_t thread,
                                 zx_thread_state_general_regs_t* regs) {
  zx_status_t status =
      zx_thread_read_state(thread, ZX_THREAD_STATE_GENERAL_REGS,
                           regs, sizeof(*regs));
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to read general registers for thread "
                   << debugger_utils::GetKoid(thread) << ": "
                   << debugger_utils::ZxErrorString(status);
  }
  return status;
}

zx_status_t WriteGeneralRegisters(zx_handle_t thread,
                                  const zx_thread_state_general_regs_t* regs) {
  zx_status_t status =
      zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS,
                            regs, sizeof(*regs));
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to write general registers for thread "
                   << debugger_utils::GetKoid(thread) << ": "
                   << debugger_utils::ZxErrorString(status);
  }
  return status;
}

}  // namespace debugger_utils
