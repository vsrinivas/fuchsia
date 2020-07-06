// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_PROCESS_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_PROCESS_HANDLE_H_

#include "src/developer/debug/debug_agent/process_handle.h"

namespace debug_agent {

class ZirconProcessHandle final : public ProcessHandle {
 public:
  ZirconProcessHandle(zx_koid_t process_koid, zx::process p);

  // ProcessHandle implementation.
  const zx::process& GetNativeHandle() const override { return process_; }
  zx::process& GetNativeHandle() override { return process_; }
  zx_koid_t GetKoid() const override { return process_koid_; }
  zx_status_t GetInfo(zx_info_process* info) const override;
  std::vector<debug_ipc::AddressRegion> GetAddressSpace(uint64_t address) const override;
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

  zx_koid_t process_koid_;
  zx::process process_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_PROCESS_HANDLE_H_
