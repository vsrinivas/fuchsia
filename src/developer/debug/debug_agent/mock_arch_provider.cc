// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_arch_provider.h"

#include <lib/syslog/cpp/macros.h>

namespace debug_agent {

zx_status_t MockArchProvider::ReadDebugState(const zx::thread& handle,
                                             zx_thread_state_debug_regs* regs) const {
  // Not implemented by this mock.
  FX_NOTREACHED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockArchProvider::WriteDebugState(const zx::thread& handle,
                                              const zx_thread_state_debug_regs& regs) {
  // Not implemented by this mock.
  FX_NOTREACHED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockArchProvider::WriteSingleStep(const zx::thread& thread, bool single_step) {
  // Not implemented by this mock.
  FX_NOTREACHED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockArchProvider::GetInfo(const zx::thread& thread, zx_object_info_topic_t topic,
                                      void* buffer, size_t buffer_size, size_t* actual = nullptr,
                                      size_t* avail = nullptr) const {
  // TODO this should be mocked instead. But currently there's no way to mock the thread passed as
  // input so there's no point in mocking the results from this.
  return thread.get_info(topic, buffer, buffer_size, actual, avail);
}

void MockArchProvider::FillExceptionRecord(const zx::thread&,
                                           debug_ipc::ExceptionRecord* out) const {
  out->valid = false;
}

}  // namespace debug_agent
