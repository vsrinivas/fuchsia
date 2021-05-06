// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SYSMEM_SECURE_MEM_SERVER_H_
#define SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SYSMEM_SECURE_MEM_SERVER_H_

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/closure-queue/closure_queue.h>
#include <threads.h>

#include <optional>

#include <tee-client-api/tee_client_api.h>

#include "secmem-session.h"

// This is used with fidl::BindSingleInFlightOnly() to dispatch fuchsia::sysmem::Tee requests.
class SysmemSecureMemServer : public fidl::WireServer<fuchsia_sysmem::SecureMem> {
 public:
  using SecureMemServerDone = fit::callback<void(bool is_success)>;

  SysmemSecureMemServer(thrd_t ddk_dispatcher_thread, zx::channel loopback_tee_client);
  ~SysmemSecureMemServer() override;

  zx_status_t BindAsync(zx::channel sysmem_secure_mem_server,
                        thrd_t* sysmem_secure_mem_server_thread,
                        SecureMemServerDone secure_mem_server_done);
  void StopAsync();

  // fidl::WireServer<fuchsia_sysmem::SecureMem> impl
  void GetPhysicalSecureHeaps(GetPhysicalSecureHeapsRequestView request,
                              GetPhysicalSecureHeapsCompleter::Sync& completer) override;
  void SetPhysicalSecureHeaps(SetPhysicalSecureHeapsRequestView request,
                              SetPhysicalSecureHeapsCompleter::Sync& completer) override;

 private:
  void PostToLoop(fit::closure to_run);

  bool TrySetupSecmemSession();
  void EnsureLoopDone(bool is_success);

  zx_status_t GetPhysicalSecureHeapsInternal(fuchsia_sysmem::wire::PhysicalSecureHeaps* heaps);
  zx_status_t SetPhysicalSecureHeapsInternal(fuchsia_sysmem::wire::PhysicalSecureHeaps heaps);

  // Call secmem TA to setup the one physical secure heap that's configured by the TEE Controller.
  zx_status_t SetupVdec(uint64_t* physical_address, uint64_t* size_bytes);

  // Call secmem TA to setup the one physical secure heap that's configured by sysmem.
  zx_status_t ProtectMemoryRange(uint64_t physical_address, uint64_t size_bytes);

  thrd_t ddk_dispatcher_thread_ = {};
  fuchsia::tee::ApplicationSyncPtr tee_connection_ = {};
  async::Loop loop_;
  thrd_t loop_thread_ = {};
  bool was_thread_started_ = {};
  bool is_loop_done_ = {};
  SecureMemServerDone secure_mem_server_done_;

  bool is_get_physical_secure_heaps_called_ = {};
  bool is_set_physical_secure_heaps_called_ = {};

  // We try to open a SecmemSession once.  If that fails, we remember the status and
  // EnsureSecmemSession() will return that status without trying Init() again.
  bool has_attempted_secmem_session_connection_ = false;
  std::optional<SecmemSession> secmem_session_ = std::nullopt;

  bool is_protect_memory_range_active_ = {};
  uint32_t protect_start_ = {};
  uint32_t protect_length_ = {};

  // Last on purpose.
  ClosureQueue closure_queue_;
};

#endif  // SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SYSMEM_SECURE_MEM_SERVER_H_
