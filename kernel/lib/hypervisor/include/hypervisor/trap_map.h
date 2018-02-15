// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/arena.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_ptr.h>
#include <hypervisor/state_invalidator.h>
#include <object/port_dispatcher.h>
#include <object/semaphore.h>

namespace hypervisor {

// Blocks on allocation if the arena is empty.
class BlockingPortAllocator final : public PortAllocator {
public:
    BlockingPortAllocator();

    zx_status_t Init() TA_NO_THREAD_SAFETY_ANALYSIS;
    PortPacket* AllocBlocking();
    virtual void Free(PortPacket* port_packet) override;

private:
    Semaphore semaphore_;
    fbl::TypedArena<PortPacket, fbl::Mutex> arena_;

    PortPacket* Alloc() override;
};

// Describes a single trap within a guest.
class Trap : public fbl::WAVLTreeContainable<fbl::unique_ptr<Trap>> {
public:
    Trap(uint32_t kind, zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
         uint64_t key);

    zx_status_t Init();
    zx_status_t Queue(const zx_port_packet_t& packet, StateInvalidator* invalidator);

    zx_vaddr_t GetKey() const { return addr_; }
    bool Contains(zx_vaddr_t val) const { return val >= addr_ && val < addr_ + len_; }
    bool HasPort() const { return !!port_; }

    uint32_t kind() const { return kind_; }
    zx_vaddr_t addr() const { return addr_; }
    size_t len() const { return len_; }
    uint64_t key() const { return key_; }

private:
    const uint32_t kind_;
    const zx_vaddr_t addr_;
    const size_t len_;
    const fbl::RefPtr<PortDispatcher> port_;
    const uint64_t key_; // Key for packets in this port range.
    BlockingPortAllocator port_allocator_;
};

// Contains all the traps within a guest.
class TrapMap {
public:
    zx_status_t InsertTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                           fbl::RefPtr<PortDispatcher> port, uint64_t key);
    zx_status_t FindTrap(uint32_t kind, zx_vaddr_t addr, Trap** trap);

private:
    using TrapTree = fbl::WAVLTree<zx_vaddr_t, fbl::unique_ptr<Trap>>;

    fbl::Mutex mutex_;
    TrapTree mem_traps_ TA_GUARDED(mutex_);
    TrapTree io_traps_ TA_GUARDED(mutex_);

    TrapTree* TreeOf(uint32_t kind);
};

} // namespace hypervisor
