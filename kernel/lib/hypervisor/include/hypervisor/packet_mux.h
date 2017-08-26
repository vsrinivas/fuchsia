// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <mxtl/arena.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/ref_ptr.h>

#if WITH_LIB_MAGENTA
#include <magenta/port_dispatcher.h>
#include <magenta/semaphore.h>
#else // WITH_LIB_MAGENTA
#include <magenta/syscalls/port.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_counted.h>
class PortDispatcher : public mxtl::RefCounted<PortDispatcher> {};
struct PortPacket;
struct PortAllocator {
    virtual PortPacket* Alloc() { return nullptr; }
    virtual void Free(PortPacket* port_packet) {}
};
struct Semaphore {
    Semaphore(int64_t initial_count) {}
    int Post() { return 0; }
    mx_status_t Wait(lk_time_t deadline) { return MX_ERR_NOT_SUPPORTED; }
};
#endif // WITH_LIB_MAGENTA

/* Reloads the hypervisor state. */
struct StateReloader {
    virtual void Reload() = 0;
};

/* Blocks on allocation if the arena is empty. */
class BlockingPortAllocator final : public PortAllocator {
public:
    BlockingPortAllocator();

    mx_status_t Init() TA_NO_THREAD_SAFETY_ANALYSIS;
    PortPacket* Alloc(StateReloader* reloader);
    virtual void Free(PortPacket* port_packet) override;

private:
    Semaphore semaphore_;
    mxtl::Mutex mutex_;
    mxtl::Arena arena_ TA_GUARDED(mutex_);

    PortPacket* Alloc() override;
};

/* Specifies an address range to associate with a port. */
class PortRange : public mxtl::WAVLTreeContainable<mxtl::unique_ptr<PortRange>> {
public:
    PortRange(mx_vaddr_t addr, size_t len, mxtl::RefPtr<PortDispatcher> port, uint64_t key);
    virtual ~PortRange(){};

    mx_status_t Init();
    mx_status_t Queue(const mx_port_packet_t& packet, StateReloader* reloader);

    mx_vaddr_t GetKey() const { return addr_; }
    bool InRange(mx_vaddr_t val) const { return val >= addr_ && val < addr_ + len_; }

private:
    const mx_vaddr_t addr_;
    const size_t len_;
    const mxtl::RefPtr<PortDispatcher> port_;
    const uint64_t key_; // Key for packets in this port range.
    BlockingPortAllocator port_allocator_;
};

/* Demultiplexes packets onto ports. */
class PacketMux {
public:
    mx_status_t AddPortRange(mx_vaddr_t addr, size_t len, mxtl::RefPtr<PortDispatcher> port,
                             uint64_t key);
    mx_status_t Queue(mx_vaddr_t addr, const mx_port_packet_t& packet, StateReloader* reloader);

private:
    using PortTree = mxtl::WAVLTree<mx_vaddr_t, mxtl::unique_ptr<PortRange>>;

    mxtl::Mutex mutex_;
    PortTree ports_ TA_GUARDED(mutex_);

    mx_status_t FindPortRange(mx_vaddr_t addr, PortRange** port_range);
};
