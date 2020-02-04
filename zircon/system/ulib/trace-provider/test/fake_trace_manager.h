// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_TESTS_FAKE_TRACE_MANAGER_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_TESTS_FAKE_TRACE_MANAGER_H_

#include <stdint.h>

#include <memory>

#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

namespace trace {
namespace test {

class FakeTraceManager {
 public:
  static void Create(async_dispatcher_t* dispatcher,
                     std::unique_ptr<FakeTraceManager>* out_manager, zx::channel* out_channel);

 private:
  FakeTraceManager(async_dispatcher_t* dispatcher, zx::channel channel);

  void Close();

  void Handle(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
              const zx_packet_signal_t* signal);

  bool ReadMessage();
  bool DecodeAndDispatch(uint8_t* buffer, uint32_t num_bytes, zx_handle_t* handles,
                         uint32_t num_handles);

  zx::channel channel_;

  async::WaitMethod<FakeTraceManager, &FakeTraceManager::Handle> wait_;
};

}  // namespace test
}  // namespace trace

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_TESTS_FAKE_TRACE_MANAGER_H_
