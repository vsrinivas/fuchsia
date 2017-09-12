// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/arena.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_ptr.h>
#include <object/port_dispatcher.h>
#include <object/semaphore.h>

/* Reloads the hypervisor state. */
struct StateReloader {
    virtual void Reload() = 0;
};

/* Blocks on allocation if the arena is empty. */
class BlockingPortAllocator final : public PortAllocator {
public:
    BlockingPortAllocator();

    zx_status_t Init() TA_NO_THREAD_SAFETY_ANALYSIS;
    PortPacket* Alloc(StateReloader* reloader);
    virtual void Free(PortPacket* port_packet) override;

private:
    Semaphore semaphore_;
    fbl::TypedArena<PortPacket, fbl::Mutex> arena_;

    PortPacket* Alloc() override;
};

/* Specifies an address range to associate with a port. */
class PortRange : public fbl::WAVLTreeContainable<fbl::unique_ptr<PortRange>> {
public:
    PortRange(zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port, uint64_t key);
    virtual ~PortRange(){};

    zx_status_t Init();
    zx_status_t Queue(const zx_port_packet_t& packet, StateReloader* reloader);

    zx_vaddr_t GetKey() const { return addr_; }
    bool InRange(zx_vaddr_t val) const { return val >= addr_ && val < addr_ + len_; }

private:
    const zx_vaddr_t addr_;
    const size_t len_;
    const fbl::RefPtr<PortDispatcher> port_;
    const uint64_t key_; // Key for packets in this port range.
    BlockingPortAllocator port_allocator_;
};

/* Demultiplexes packets onto ports. */
class PacketMux {
public:
    zx_status_t AddPortRange(zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
                             uint64_t key);
    zx_status_t Queue(zx_vaddr_t addr, const zx_port_packet_t& packet, StateReloader* reloader);

private:
    using PortTree = fbl::WAVLTree<zx_vaddr_t, fbl::unique_ptr<PortRange>>;

    fbl::Mutex mutex_;
    PortTree ports_ TA_GUARDED(mutex_);

    zx_status_t FindPortRange(zx_vaddr_t addr, PortRange** port_range);
};
