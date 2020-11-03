// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_HANDLE_H_

#include <lib/fit/function.h>
#include <lib/fitx/result.h>
#include <lib/zx/process.h>

#include <memory>
#include <vector>

namespace debug_ipc {
struct AddressRegion;
struct MemoryBlock;
struct Module;
struct InfoHandle;
}  // namespace debug_ipc

namespace debug_agent {

class ProcessHandleObserver;
class ThreadHandle;

class ProcessHandle {
 public:
  virtual ~ProcessHandle() = default;

  // Access to the underlying native process object. This is for porting purposes, ideally this
  // object would encapsulate all details about the process for testing purposes and this getter
  // would be removed. In testing situations, the returned value may be an empty object,
  // TODO(brettw) Remove this.
  virtual const zx::process& GetNativeHandle() const = 0;
  virtual zx::process& GetNativeHandle() = 0;

  virtual zx_koid_t GetKoid() const = 0;
  virtual std::string GetName() const = 0;

  virtual std::vector<std::unique_ptr<ThreadHandle>> GetChildThreads() const = 0;

  // Terminates the process. The actually termination will normally happen asynchronously.
  virtual zx_status_t Kill() = 0;

  // Retrieves the return code for an exited process. Returns some default value if the process is
  // still running (as defined by the kernel).
  virtual int64_t GetReturnCode() const = 0;

  // Registers for process notifications on the given interface. The pointer must outlive this class
  // or until Detach() is called. The observer nust not be null (use Detach() instead). Calling
  // multiple times will replace the observer pointer.
  virtual zx_status_t Attach(ProcessHandleObserver* observer) = 0;

  // Unregisters for process notifications. See Attach(). It is legal to call Detach() multiple
  // times or when not already attached.
  virtual void Detach() = 0;

  // Returns the address space information. If the address is non-null, only the regions covering
  // that address will be returned. Otherwise all regions will be returned.
  virtual std::vector<debug_ipc::AddressRegion> GetAddressSpace(uint64_t address) const = 0;

  // Returns the modules (shared libraries and the main binary) for the process. Will be empty on
  // failure.
  //
  // Prefer this version to calling the elf_utils variant because this one allows mocking.
  //
  // TODO(brettw) consider moving dl_debug_addr to be internally managed by ZirconProcessInfo.
  virtual std::vector<debug_ipc::Module> GetModules(uint64_t dl_debug_addr) const = 0;

  // Returns the handles opened by the process.
  virtual fitx::result<zx_status_t, std::vector<debug_ipc::InfoHandle>> GetHandles() const = 0;

  virtual zx_status_t ReadMemory(uintptr_t address, void* buffer, size_t len,
                                 size_t* actual) const = 0;
  virtual zx_status_t WriteMemory(uintptr_t address, const void* buffer, size_t len,
                                  size_t* actual) = 0;

  // Does a mapped-memory-aware read of the process memory. The result can contain holes which
  // the normal ReadMemory call above can't handle. On failure, there will be one block returned
  // covering the requested size, marked invalid.
  virtual std::vector<debug_ipc::MemoryBlock> ReadMemoryBlocks(uint64_t address,
                                                               uint32_t size) const = 0;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_HANDLE_H_
