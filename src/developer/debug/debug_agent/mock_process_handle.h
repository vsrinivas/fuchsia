// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_HANDLE_H_

#include <zircon/status.h>
#include <zircon/types.h>

#include <string>

#include "src/developer/debug/debug_agent/mock_thread_handle.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/shared/mock_memory.h"

namespace debug_agent {

class MockProcessHandle final : public ProcessHandle {
 public:
  struct MemoryWrite {
    MemoryWrite(uint64_t a, std::vector<uint8_t> d) : address(a), data(std::move(d)) {}

    uint64_t address = 0;
    std::vector<uint8_t> data;
  };

  explicit MockProcessHandle(zx_koid_t process_koid, std::string name = std::string());

  void set_name(std::string n) { name_ = std::move(n); }
  void set_job_koid(zx_koid_t koid) { job_koid_ = koid; }

  // Sets the threads. These will be copied since we need to return a new unique_ptr for each call
  // to GetChildThreads().
  void set_threads(std::vector<MockThreadHandle> threads) { threads_ = std::move(threads); }

  // Use to set mcoked memory values to read. The MockMemory is only used for ReadMemory calls.
  // WriteMemory calls come out in memory_writes().
  debug::MockMemory& mock_memory() { return mock_memory_; }
  std::vector<MemoryWrite>& memory_writes() { return memory_writes_; }

  // Value to return from Kill().
  void set_kill_status(debug::Status s) { kill_status_ = std::move(s); }

  bool is_attached() const { return is_attached_; }

  // ProcessHandle implementation.
  const zx::process& GetNativeHandle() const override { return null_handle_; }
  zx::process& GetNativeHandle() override { return null_handle_; }
  zx_koid_t GetKoid() const override { return process_koid_; }
  std::string GetName() const override { return name_; }
  std::vector<std::unique_ptr<ThreadHandle>> GetChildThreads() const override;
  zx_koid_t GetJobKoid() const override { return job_koid_; }
  debug::Status Kill() override;
  int64_t GetReturnCode() const override;
  debug::Status Attach(ProcessHandleObserver* observer) override;
  void Detach() override;
  uint64_t GetLoaderBreakpointAddress() override;
  std::vector<debug_ipc::AddressRegion> GetAddressSpace(uint64_t address) const override;
  std::vector<debug_ipc::Module> GetModules() const override;
  fit::result<debug::Status, std::vector<debug_ipc::InfoHandle>> GetHandles() const override;
  debug::Status ReadMemory(uintptr_t address, void* buffer, size_t len,
                           size_t* actual) const override;
  debug::Status WriteMemory(uintptr_t address, const void* buffer, size_t len,
                            size_t* actual) override;
  std::vector<debug_ipc::MemoryBlock> ReadMemoryBlocks(uint64_t address,
                                                       uint32_t size) const override;
  debug::Status SaveMinidump(const std::vector<DebuggedThread*>& threads,
                             std::vector<uint8_t>* core_data) override;

 private:
  // Always null, for returning only from the getters above.
  // TODO(brettw) Remove this when the ThreadHandle no longer exposes a zx::thread getter.
  static zx::process null_handle_;

  zx_koid_t process_koid_;
  zx_koid_t job_koid_ = ZX_KOID_INVALID;
  std::string name_;

  bool is_attached_ = false;

  std::vector<MockThreadHandle> threads_;

  debug::MockMemory mock_memory_;
  std::vector<MemoryWrite> memory_writes_;

  debug::Status kill_status_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_HANDLE_H_
