// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/trap_map.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hypervisor/ktrace.h>
#include <lib/ktrace.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>

static constexpr size_t kMaxPacketsPerRange = 256;

namespace hypervisor {

BlockingPortAllocator::BlockingPortAllocator() : semaphore_(kMaxPacketsPerRange) {}

zx_status_t BlockingPortAllocator::Init() {
    return arena_.Init("hypervisor-packets", kMaxPacketsPerRange);
}

PortPacket* BlockingPortAllocator::AllocBlocking() {
    ktrace_vcpu(TAG_VCPU_BLOCK, VCPU_PORT);
    zx_status_t status = semaphore_.Wait(ZX_TIME_INFINITE, nullptr);
    ktrace_vcpu(TAG_VCPU_UNBLOCK, VCPU_PORT);
    if (status != ZX_OK)
        return nullptr;
    return Alloc();
}

PortPacket* BlockingPortAllocator::Alloc() {
    return arena_.New(nullptr, this);
}

void BlockingPortAllocator::Free(PortPacket* port_packet) {
    arena_.Delete(port_packet);
    if (semaphore_.Post() > 0)
        thread_reschedule();
}

Trap::Trap(uint32_t kind, zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
                     uint64_t key)
    : kind_(kind), addr_(addr), len_(len), port_(fbl::move(port)), key_(key) {
    (void) key_;
}

zx_status_t Trap::Init() {
    return port_allocator_.Init();
}

zx_status_t Trap::Queue(const zx_port_packet_t& packet, StateInvalidator* invalidator) {
    if (invalidator != nullptr)
        invalidator->Invalidate();
    if (port_ == nullptr)
        return ZX_ERR_NOT_FOUND;
    PortPacket* port_packet = port_allocator_.AllocBlocking();
    if (port_packet == nullptr)
        return ZX_ERR_NO_MEMORY;
    port_packet->packet = packet;
    zx_status_t status = port_->Queue(port_packet, ZX_SIGNAL_NONE, 0);
    if (status != ZX_OK)
        port_allocator_.Free(port_packet);
    return status;
}

zx_status_t TrapMap::InsertTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                                fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    TrapTree* traps = TreeOf(kind);
    if (traps == nullptr)
        return ZX_ERR_INVALID_ARGS;
    auto iter = traps->find(addr);
    if (iter.IsValid()) {
        dprintf(INFO, "Port range for kind %u (addr %#lx len %lu key %lu) already exists "
                "(addr %#lx len %lu key %lu)\n", kind, addr, len, key, iter->addr(), iter->len(),
                iter->key());
        return ZX_ERR_ALREADY_EXISTS;
    }
    fbl::AllocChecker ac;
    fbl::unique_ptr<Trap> range(new (&ac) Trap(kind, addr, len, fbl::move(port), key));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    zx_status_t status = range->Init();
    if (status != ZX_OK)
        return status;
    {
        fbl::AutoLock lock(&mutex_);
        traps->insert(fbl::move(range));
    }
    return ZX_OK;
}

zx_status_t TrapMap::FindTrap(uint32_t kind, zx_vaddr_t addr, Trap** trap) {
    TrapTree* traps = TreeOf(kind);
    if (traps == nullptr)
        return ZX_ERR_INVALID_ARGS;
    TrapTree::iterator iter;
    {
        fbl::AutoLock lock(&mutex_);
        iter = traps->upper_bound(addr);
    }
    --iter;
    if (!iter.IsValid() || !iter->Contains(addr))
        return ZX_ERR_NOT_FOUND;
    *trap = const_cast<Trap*>(&*iter);
    return ZX_OK;
}

TrapMap::TrapTree* TrapMap::TreeOf(uint32_t kind) {
    switch (kind) {
    case ZX_GUEST_TRAP_BELL:
    case ZX_GUEST_TRAP_MEM:
        return &mem_traps_;
    case ZX_GUEST_TRAP_IO:
        return &io_traps_;
    default:
        return nullptr;
    }
}

} // namespace hypervisor
