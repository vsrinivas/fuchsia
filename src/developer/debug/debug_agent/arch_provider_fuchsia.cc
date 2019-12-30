// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_provider_fuchsia.h"

#include <lib/zx/thread.h>

namespace debug_agent {
namespace arch {

zx_status_t ArchProviderFuchsia::ReadGeneralState(const zx::thread& thread,
                                                  zx_thread_state_general_regs* regs) const {
  return thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, regs,
                           sizeof(zx_thread_state_general_regs));
}

zx_status_t ArchProviderFuchsia::WriteGeneralState(const zx::thread& thread,
                                                   const zx_thread_state_general_regs& regs) {
  return thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs,
                            sizeof(zx_thread_state_general_regs));
}

zx_status_t ArchProviderFuchsia::ReadDebugState(const zx::thread& thread,
                                                zx_thread_state_debug_regs* regs) const {
  return thread.read_state(ZX_THREAD_STATE_DEBUG_REGS, regs, sizeof(zx_thread_state_debug_regs));
}

zx_status_t ArchProviderFuchsia::WriteDebugState(const zx::thread& thread,
                                                 const zx_thread_state_debug_regs& regs) {
  return thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(zx_thread_state_debug_regs));
}

zx_status_t ArchProviderFuchsia::WriteSingleStep(const zx::thread& thread, bool single_step) {
  zx_thread_state_single_step_t value = single_step ? 1 : 0;
  // This could fail for legitimate reasons, like the process could have just closed the thread.
  return thread.write_state(ZX_THREAD_STATE_SINGLE_STEP, &value, sizeof(value));
}

zx_status_t ArchProviderFuchsia::GetInfo(const zx::thread& thread, zx_object_info_topic_t topic,
                                         void* buffer, size_t buffer_size, size_t* actual,
                                         size_t* avail) const {
  return thread.get_info(topic, buffer, buffer_size, actual, avail);
}

}  // namespace arch
}  // namespace debug_agent
