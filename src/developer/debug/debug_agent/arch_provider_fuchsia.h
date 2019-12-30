// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_PROVIDER_FUCHSIA_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_PROVIDER_FUCHSIA_H_

#include "src/developer/debug/debug_agent/arch.h"

namespace debug_agent {
namespace arch {

// Provides the concrete implementation of functions that talk to the OS for the CPU-specific
// Arch implementations.
class ArchProviderFuchsia : public ArchProvider {
 public:
  // ArchProvider implementation.
  zx_status_t ReadGeneralState(const zx::thread& handle,
                               zx_thread_state_general_regs* regs) const override;
  zx_status_t WriteGeneralState(const zx::thread& handle,
                                const zx_thread_state_general_regs& regs) override;
  zx_status_t ReadDebugState(const zx::thread& handle,
                             zx_thread_state_debug_regs* regs) const override;
  zx_status_t WriteDebugState(const zx::thread& handle,
                              const zx_thread_state_debug_regs& regs) override;
  zx_status_t WriteSingleStep(const zx::thread& thread, bool single_step) override;
  zx_status_t GetInfo(const zx::thread&, zx_object_info_topic_t topic, void* buffer,
                      size_t buffer_size, size_t* actual = nullptr,
                      size_t* avail = nullptr) const override;
};

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_PROVIDER_FUCHSIA_H_
