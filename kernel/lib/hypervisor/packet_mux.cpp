// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/packet_mux.h>

#include <magenta/syscalls/hypervisor.h>
#include <mxtl/alloc_checker.h>
#include <mxtl/auto_lock.h>

__UNUSED static const size_t kMaxPacketsPerRange = 256;

BlockingPortAllocator::BlockingPortAllocator() : semaphore_(kMaxPacketsPerRange) {}

mx_status_t BlockingPortAllocator::Init() {
#if WITH_LIB_MAGENTA
    return arena_.Init("hypervisor-packets", sizeof(PortPacket), kMaxPacketsPerRange);
#else
    return MX_ERR_NOT_SUPPORTED;
#endif // WITH_LIB_MAGENTA
}

PortPacket* BlockingPortAllocator::Alloc(StateReloader* reloader) {
    PortPacket* port_packet = Alloc();
    mx_status_t status = semaphore_.Wait(INFINITE_TIME);
    // If port_packet is NULL, then Wait would have blocked. So we need to:
    // reload our state, check the status of Wait, and Alloc again.
    if (port_packet == nullptr) {
        reloader->Reload();
        if (status != MX_OK)
            return nullptr;
        port_packet = Alloc();
    }
    return port_packet;
}

PortPacket* BlockingPortAllocator::Alloc() {
#if WITH_LIB_MAGENTA
    void* addr;
    {
        mxtl::AutoLock lock(&mutex_);
        addr = arena_.Alloc();
    }
    if (addr == nullptr)
        return nullptr;
    return new (addr) PortPacket(nullptr, this);
#else
    return nullptr;
#endif // WITH_LIB_MAGENTA
}

void BlockingPortAllocator::Free(PortPacket* port_packet) {
    {
        mxtl::AutoLock lock(&mutex_);
        arena_.Free(port_packet);
    }
    if (semaphore_.Post() > 0)
        thread_reschedule();
}

PortRange::PortRange(mx_vaddr_t addr, size_t len, mxtl::RefPtr<PortDispatcher> port, uint64_t key)
    : addr_(addr), len_(len), port_(mxtl::move(port)), key_(key) {
    (void) key_;
}

mx_status_t PortRange::Init() {
    return port_allocator_.Init();
}

mx_status_t PortRange::Queue(const mx_port_packet_t& packet, StateReloader* reloader) {
#if WITH_LIB_MAGENTA
    PortPacket* port_packet = port_allocator_.Alloc(reloader);
    if (port_packet == nullptr)
        return MX_ERR_NO_MEMORY;
    port_packet->packet = packet;
    port_packet->packet.key = key_;
    port_packet->packet.type |= PKT_FLAG_EPHEMERAL;
    mx_status_t status = port_->Queue(port_packet, MX_SIGNAL_NONE, 0);
    if (status != MX_OK)
        port_allocator_.Free(port_packet);
    return status;
#else
    return MX_ERR_NOT_SUPPORTED;
#endif // WITH_LIB_MAGENTA
}

mx_status_t PacketMux::AddPortRange(mx_vaddr_t addr, size_t len,
                                    mxtl::RefPtr<PortDispatcher> port, uint64_t key) {
    mxtl::AllocChecker ac;
    mxtl::unique_ptr<PortRange> range(new (&ac) PortRange(addr, len, mxtl::move(port), key));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;
    mx_status_t status = range->Init();
    if (status != MX_OK)
        return status;
    {
        mxtl::AutoLock lock(&mutex_);
        ports_.insert(mxtl::move(range));
    }
    return MX_OK;
}

mx_status_t PacketMux::FindPortRange(mx_vaddr_t addr, PortRange** port_range) {
    PortTree::iterator iter;
    {
        mxtl::AutoLock lock(&mutex_);
        iter = ports_.upper_bound(addr);
    }
    --iter;
    if (!iter.IsValid() || !iter->InRange(addr))
        return MX_ERR_NOT_FOUND;
    *port_range = const_cast<PortRange*>(&*iter);
    return MX_OK;
}

mx_status_t PacketMux::Queue(mx_vaddr_t addr, const mx_port_packet_t& packet,
                             StateReloader* reloader) {
    PortRange* port_range;
    mx_status_t status = FindPortRange(addr, &port_range);
    if (status != MX_OK)
        return status;
    return port_range->Queue(packet, reloader);
}
