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

debug::Status MockProcessHandle::Kill() { return kill_status_; }

int64_t MockProcessHandle::GetReturnCode() const { return 0; }

debug::Status MockProcessHandle::Attach(ProcessHandleObserver* observer) {
  is_attached_ = true;
  return debug::Status();
}

void MockProcessHandle::Detach() { is_attached_ = false; }

uint64_t MockProcessHandle::GetLoaderBreakpointAddress() {
  // Not currently implemented in this mock.
  return 0;
}

std::vector<debug_ipc::AddressRegion> MockProcessHandle::GetAddressSpace(uint64_t address) const {
  // Not currently implemented in this mock.
  return {};
}

std::vector<debug_ipc::Module> MockProcessHandle::GetModules() const {
  // Not currently implemented in this mock.
  return {};
}

fit::result<debug::Status, std::vector<debug_ipc::InfoHandle>> MockProcessHandle::GetHandles()
    const {
  // Not currently implemented in this mock.
  return fit::success(std::vector<debug_ipc::InfoHandle>());
}

debug::Status MockProcessHandle::ReadMemory(uintptr_t address, void* buffer, size_t len,
                                            size_t* actual) const {
  auto vect = mock_memory_.ReadMemory(address, len);
  if (!vect.empty())
    memcpy(buffer, vect.data(), vect.size());
  *actual = vect.size();
  return debug::Status();
}

debug::Status MockProcessHandle::WriteMemory(uintptr_t address, const void* buffer, size_t len,
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
  return debug::Status();
}

std::vector<debug_ipc::MemoryBlock> MockProcessHandle::ReadMemoryBlocks(uint64_t address,
                                                                        uint32_t size) const {
  auto mem_vect = mock_memory_.ReadMemory(address, size);
  debug_ipc::MemoryBlock mem_block;
  mem_block.data = std::move(mem_vect);
  mem_block.size = size;
  mem_block.valid = true;
  mem_block.address = address;
  std::vector<debug_ipc::MemoryBlock> mem_blocks;
  mem_blocks.emplace_back(std::move(mem_block));
  return mem_blocks;
}

debug::Status MockProcessHandle::SaveMinidump(const std::vector<DebuggedThread*>& threads,
                                              std::vector<uint8_t>* core_data) {
  // Not currently implemented.
  return debug::Status();
}

}  // namespace debug_agent
