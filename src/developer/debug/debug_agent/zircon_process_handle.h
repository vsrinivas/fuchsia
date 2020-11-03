// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_PROCESS_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_PROCESS_HANDLE_H_

#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zircon_exception_watcher.h"

namespace debug_agent {

class ZirconProcessHandle final : public ProcessHandle, public debug_ipc::ZirconExceptionWatcher {
 public:
  explicit ZirconProcessHandle(zx::process p);

  // ProcessHandle implementation.
  const zx::process& GetNativeHandle() const override { return process_; }
  zx::process& GetNativeHandle() override { return process_; }
  zx_koid_t GetKoid() const override { return process_koid_; }
  std::string GetName() const override;
  std::vector<std::unique_ptr<ThreadHandle>> GetChildThreads() const override;
  zx_status_t Kill() override;
  int64_t GetReturnCode() const override;
  zx_status_t Attach(ProcessHandleObserver* observer) override;
  void Detach() override;
  std::vector<debug_ipc::AddressRegion> GetAddressSpace(uint64_t address) const override;
  std::vector<debug_ipc::Module> GetModules(uint64_t dl_debug_addr) const override;
  fitx::result<zx_status_t, std::vector<debug_ipc::InfoHandle>> GetHandles() const override;
  zx_status_t ReadMemory(uintptr_t address, void* buffer, size_t len,
                         size_t* actual) const override;
  zx_status_t WriteMemory(uintptr_t address, const void* buffer, size_t len,
                          size_t* actual) override;
  std::vector<debug_ipc::MemoryBlock> ReadMemoryBlocks(uint64_t address,
                                                       uint32_t size) const override;

 private:
  // Reads one memory block from the process. block.valid will be unset on failure.
  debug_ipc::MemoryBlock ReadOneMemoryBlock(uint64_t address, uint32_t size) const;

  // Gets all memory maps for this process.
  std::vector<zx_info_maps_t> GetMaps() const;

  // ZirconExceptionWatcher implementation.
  void OnProcessTerminated(zx_koid_t process_koid) override;
  void OnThreadStarting(zx::exception exception_token, zx_exception_info_t info) override;
  void OnThreadExiting(zx::exception exception_token, zx_exception_info_t info) override;
  void OnException(zx::exception exception_token, zx_exception_info_t info) override;

  zx_koid_t process_koid_;
  zx::process process_;

  ProcessHandleObserver* observer_ = nullptr;  // Null means no observer to notify.

  // Handle for watching the process exceptions.
  debug_ipc::MessageLoop::WatchHandle process_watch_handle_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_PROCESS_HANDLE_H_
