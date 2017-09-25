// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/packet_mux.h>

#include <zircon/syscalls/hypervisor.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

__UNUSED static const size_t kMaxPacketsPerRange = 256;

BlockingPortAllocator::BlockingPortAllocator() : semaphore_(kMaxPacketsPerRange) {}

zx_status_t BlockingPortAllocator::Init() {
    return arena_.Init("hypervisor-packets", kMaxPacketsPerRange);
}

PortPacket* BlockingPortAllocator::Alloc(StateReloader* reloader) {
    PortPacket* port_packet = Alloc();
    zx_status_t status = semaphore_.Wait(INFINITE_TIME);
    // If port_packet is NULL, then Wait would have blocked. So we need to:
    // reload our state, check the status of Wait, and Alloc again.
    if (port_packet == nullptr) {
        reloader->Reload();
        if (status != ZX_OK)
            return nullptr;
        port_packet = Alloc();
    }
    return port_packet;
}

PortPacket* BlockingPortAllocator::Alloc() {
    return arena_.New(nullptr, this);
}

void BlockingPortAllocator::Free(PortPacket* port_packet) {
    arena_.Delete(port_packet);
    if (semaphore_.Post() > 0)
        thread_reschedule();
}

PortRange::PortRange(uint32_t kind, zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
                     uint64_t key)
    : kind_(kind), addr_(addr), len_(len), port_(fbl::move(port)), key_(key) {
    (void) key_;
}

zx_status_t PortRange::Init() {
    return port_allocator_.Init();
}

zx_status_t PortRange::Queue(const zx_port_packet_t& packet, StateReloader* reloader) {
    if (port_ == nullptr)
        return ZX_ERR_NOT_FOUND;
    PortPacket* port_packet = port_allocator_.Alloc(reloader);
    if (port_packet == nullptr)
        return ZX_ERR_NO_MEMORY;
    port_packet->packet = packet;
    port_packet->packet.key = key_;
    port_packet->packet.type |= PKT_FLAG_EPHEMERAL;
    zx_status_t status = port_->Queue(port_packet, ZX_SIGNAL_NONE, 0);
    if (status != ZX_OK)
        port_allocator_.Free(port_packet);
    return status;
}

zx_status_t PacketMux::AddPortRange(uint32_t kind, zx_vaddr_t addr, size_t len,
                                    fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    PortTree* ports = TreeOf(kind);
    if (ports == nullptr)
        return ZX_ERR_INVALID_ARGS;
    fbl::AllocChecker ac;
    fbl::unique_ptr<PortRange> range(new (&ac) PortRange(kind, addr, len, fbl::move(port), key));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    zx_status_t status = range->Init();
    if (status != ZX_OK)
        return status;
    {
        fbl::AutoLock lock(&mutex_);
        ports->insert(fbl::move(range));
    }
    return ZX_OK;
}

zx_status_t PacketMux::FindPortRange(uint32_t kind, zx_vaddr_t addr, PortRange** port_range) {
    PortTree* ports = TreeOf(kind);
    if (ports == nullptr)
        return ZX_ERR_INVALID_ARGS;
    PortTree::iterator iter;
    {
        fbl::AutoLock lock(&mutex_);
        iter = ports->upper_bound(addr);
    }
    --iter;
    if (!iter.IsValid() || !iter->InRange(addr))
        return ZX_ERR_NOT_FOUND;
    *port_range = const_cast<PortRange*>(&*iter);
    return ZX_OK;
}

zx_status_t PacketMux::Queue(uint32_t kind, zx_vaddr_t addr, const zx_port_packet_t& packet,
                             StateReloader* reloader) {
    PortRange* port_range;
    zx_status_t status = FindPortRange(kind, addr, &port_range);
    if (status != ZX_OK)
        return status;

    DEBUG_ASSERT(port_range->HasPort());
    return port_range->Queue(packet, reloader);
}

PacketMux::PortTree* PacketMux::TreeOf(uint32_t kind) {
    switch (kind) {
    case ZX_GUEST_TRAP_BELL:
    case ZX_GUEST_TRAP_MEM:
        return &mem_ports_;
    case ZX_GUEST_TRAP_IO:
        return &io_ports_;
    default:
        return nullptr;
    }
}
