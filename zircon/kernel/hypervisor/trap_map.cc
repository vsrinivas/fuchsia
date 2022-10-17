// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/ktrace.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hypervisor/ktrace.h>
#include <hypervisor/trap_map.h>
#include <kernel/range_check.h>

namespace {

constexpr size_t kMaxPacketsPerRange = 256;

bool ValidRange(uint32_t kind, zx_gpaddr_t addr, size_t len) {
  if (len == 0) {
    return false;
  }
  zx_gpaddr_t end;
  if (add_overflow(addr, len, &end)) {
    return false;
  }
#ifdef ARCH_X86
  if (kind == ZX_GUEST_TRAP_IO && end > UINT16_MAX) {
    return false;
  }
#endif  // ARCH_X86
  return true;
}

}  // namespace

namespace hypervisor {

BlockingPortAllocator::BlockingPortAllocator() : semaphore_(kMaxPacketsPerRange) {}

zx::result<> BlockingPortAllocator::Init() {
  zx_status_t status = arena_.Init("hypervisor-packets", kMaxPacketsPerRange);
  return zx::make_result(status);
}

PortPacket* BlockingPortAllocator::AllocBlocking() {
  ktrace_vcpu(TAG_VCPU_BLOCK, VCPU_PORT);
  zx_status_t status = semaphore_.Wait(Deadline::infinite());
  ktrace_vcpu(TAG_VCPU_UNBLOCK, VCPU_PORT);
  if (status != ZX_OK) {
    return nullptr;
  }
  return Alloc();
}

PortPacket* BlockingPortAllocator::Alloc() {
  return arena_.New(this /* handle */, this /* allocator */);
}

void BlockingPortAllocator::Free(PortPacket* port_packet) {
  arena_.Delete(port_packet);
  semaphore_.Post();
}

Trap::Trap(uint32_t kind, zx_gpaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
           uint64_t key)
    : kind_(kind), addr_(addr), len_(len), port_(ktl::move(port)), key_(key) {}

Trap::~Trap() {
  if (port_ == nullptr) {
    return;
  }
  port_->CancelQueued(&port_allocator_ /* handle */, key_);
}

zx::result<> Trap::Init() { return port_allocator_.Init(); }

zx::result<> Trap::Queue(const zx_port_packet_t& packet, StateInvalidator* invalidator) {
  if (invalidator != nullptr) {
    invalidator->Invalidate();
  }
  if (port_ == nullptr) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  PortPacket* port_packet = port_allocator_.AllocBlocking();
  if (port_packet == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  port_packet->packet = packet;
  zx_status_t status = port_->Queue(port_packet, ZX_SIGNAL_NONE);
  if (status != ZX_OK) {
    port_allocator_.Free(port_packet);
    if (status == ZX_ERR_BAD_HANDLE) {
      // If the last handle to the port has been closed, then we're in a bad state.
      status = ZX_ERR_BAD_STATE;
    }
  }
  return zx::make_result(status);
}

zx::result<> TrapMap::InsertTrap(uint32_t kind, zx_gpaddr_t addr, size_t len,
                                 fbl::RefPtr<PortDispatcher> port, uint64_t key) {
  if (!ValidRange(kind, addr, len)) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  TrapTree* traps = TreeOf(kind);
  if (traps == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<Trap> range(new (&ac) Trap(kind, addr, len, ktl::move(port), key));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  if (auto result = range->Init(); result.is_error()) {
    return result.take_error();
  }

  Guard<SpinLock, IrqSave> guard{&lock_};
  auto iter = traps->upper_bound(addr);
  // If `upper_bound()` does not return `end()`, check if the range intersects.
  if (iter.IsValid() && Intersects(addr, len, iter->addr(), iter->len())) {
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }
  // Decrement the iterator, and check if the next range intersects.
  if (--iter; iter.IsValid() && Intersects(addr, len, iter->addr(), iter->len())) {
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }
  traps->insert(ktl::move(range));
  return zx::ok();
}

zx::result<Trap*> TrapMap::FindTrap(uint32_t kind, zx_gpaddr_t addr) {
  TrapTree* traps = TreeOf(kind);
  if (traps == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  Trap* found;
  {
    Guard<SpinLock, IrqSave> guard{&lock_};
    auto iter = --traps->upper_bound(addr);
    if (!iter.IsValid()) {
      return zx::error(ZX_ERR_NOT_FOUND);
    }
    found = &*iter;
  }
  if (!found->Contains(addr)) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(found);
}

TrapMap::TrapTree* TrapMap::TreeOf(uint32_t kind) {
  switch (kind) {
    case ZX_GUEST_TRAP_BELL:
    case ZX_GUEST_TRAP_MEM:
      return &mem_traps_;
#ifdef ARCH_X86
    case ZX_GUEST_TRAP_IO:
      return &io_traps_;
#endif  // ARCH_X86
    default:
      return nullptr;
  }
}

}  // namespace hypervisor
