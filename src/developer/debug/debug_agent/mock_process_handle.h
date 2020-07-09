// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_HANDLE_H_

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

  explicit MockProcessHandle(zx_koid_t process_koid);

  // Use to set mcoked memory values to read. The MockMemory is only used for ReadMemory calls.
  // WriteMemory calls come out in memory_writes().
  debug_ipc::MockMemory& mock_memory() { return mock_memory_; }
  std::vector<MemoryWrite>& memory_writes() { return memory_writes_; }

  // ProcessHandle implementation.
  const zx::process& GetNativeHandle() const override { return null_handle_; }
  zx::process& GetNativeHandle() override { return null_handle_; }
  zx_koid_t GetKoid() const override { return process_koid_; }
  int64_t GetReturnCode() const override;
  std::vector<debug_ipc::AddressRegion> GetAddressSpace(uint64_t address) const override;
  std::vector<debug_ipc::Module> GetModules(uint64_t dl_debug_addr) const override;
  zx_status_t ReadMemory(uintptr_t address, void* buffer, size_t len,
                         size_t* actual) const override;
  zx_status_t WriteMemory(uintptr_t address, const void* buffer, size_t len,
                          size_t* actual) override;
  std::vector<debug_ipc::MemoryBlock> ReadMemoryBlocks(uint64_t address,
                                                       uint32_t size) const override;

 private:
  // Always null, for returning only from the getters above.
  // TODO(brettw) Remove this when the ThreadHandle no longer exposes a zx::thread getter.
  zx::process null_handle_;

  zx_koid_t process_koid_;

  debug_ipc::MockMemory mock_memory_;
  std::vector<MemoryWrite> memory_writes_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_HANDLE_H_
