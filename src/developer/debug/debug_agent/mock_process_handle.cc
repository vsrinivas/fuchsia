// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_process_handle.h"

#include <string.h>

#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

zx::process MockProcessHandle::null_handle_;

MockProcessHandle::MockProcessHandle(zx_koid_t process_koid, std::string name)
    : process_koid_(process_koid), name_(std::move(name)) {
  // Tests could accidentally write to this handle since it's returned as a mutable value in some
  // cases. Catch accidents like that.
  FX_DCHECK(!null_handle_);
}

std::vector<std::unique_ptr<ThreadHandle>> MockProcessHandle::GetChildThreads() const {
  // Need to return a unique set of objects every time so make copies.
  std::vector<std::unique_ptr<ThreadHandle>> result;
  for (auto& thread : threads_)
    result.push_back(std::make_unique<MockThreadHandle>(thread));
  return result;
}

zx_status_t MockProcessHandle::Kill() { return kill_status_; }

int64_t MockProcessHandle::GetReturnCode() const { return 0; }

zx_status_t MockProcessHandle::Attach(ProcessHandleObserver* observer) {
  is_attached_ = true;
  return ZX_OK;
}

void MockProcessHandle::Detach() { is_attached_ = false; }

std::vector<debug_ipc::AddressRegion> MockProcessHandle::GetAddressSpace(uint64_t address) const {
  // Not currently implemented in this mock.
  return {};
}

std::vector<debug_ipc::Module> MockProcessHandle::GetModules(uint64_t dl_debug_addr) const {
  // Not currently implemented in this mock.
  return {};
}

fitx::result<zx_status_t, std::vector<debug_ipc::InfoHandle>> MockProcessHandle::GetHandles()
    const {
  // Not currently implemented in this mock.
  return fitx::success(std::vector<debug_ipc::InfoHandle>());
}

zx_status_t MockProcessHandle::ReadMemory(uintptr_t address, void* buffer, size_t len,
                                          size_t* actual) const {
  auto vect = mock_memory_.ReadMemory(address, len);
  if (!vect.empty())
    memcpy(buffer, &vect[0], vect.size());
  *actual = vect.size();
  return ZX_OK;
}

zx_status_t MockProcessHandle::WriteMemory(uintptr_t address, const void* buffer, size_t len,
                                           size_t* actual) {
  // This updates the underlying memory object to account for the change. Otherwise some tests
  // become much more complex because they have to manually manage the memory expected by the
  // code under test.
  //
  // The MockMemory object isn't necessarily designed for this and there will be some limitations.
  // Calling AddMemory adds that span to the mapped memory, but does not necessarily combine it with
  // other spans. If a larger region of memory is requested, the results may be invalid, but if the
  // same sized block is always read and written, it will be fine. Since our main test use is for
  // writing breakpoints which always use fixed sizes, this works fine for now. If this limitation
  // is a problem, we should enhance MockMemory.
  const uint8_t* src = static_cast<const uint8_t*>(buffer);
  mock_memory_.AddMemory(address, std::vector<uint8_t>(src, src + len));

  memory_writes_.emplace_back(address, std::vector<uint8_t>(src, &src[len]));
  return ZX_OK;
}

std::vector<debug_ipc::MemoryBlock> MockProcessHandle::ReadMemoryBlocks(uint64_t address,
                                                                        uint32_t size) const {
  // Not currently implemented in this mock.
  return {};
}

}  // namespace debug_agent
