// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/ref_ptr.h>

#if WITH_LIB_MAGENTA
#include <magenta/fifo_dispatcher.h>
#else
#include <magenta/types.h>
#include <mxtl/auto_lock.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_counted.h>
class FifoDispatcher : public mxtl::RefCounted<FifoDispatcher> {};
#endif // WITH_LIB_MAGENTA

typedef struct mx_guest_packet mx_guest_packet_t;

/* Reloads the hypervisor state. */
struct StateReloader {
    virtual void Reload() = 0;
};

/* Demultiplexes packets onto FIFOs. */
class PacketMux {
public:
    status_t AddFifo(mx_vaddr_t addr, size_t len, mxtl::RefPtr<FifoDispatcher> fifo);
    status_t Write(mx_vaddr_t addr, const mx_guest_packet_t& packet, StateReloader* reloader) const;

private:
    class FifoRange : public mxtl::WAVLTreeContainable<mxtl::unique_ptr<FifoRange>> {
    public:
        FifoRange(mx_vaddr_t addr, size_t len, mxtl::RefPtr<FifoDispatcher> fifo)
            : addr_(addr), len_(len), fifo_(fifo) {}

        mx_vaddr_t GetKey() const { return addr_; }
        bool InRange(mx_vaddr_t val) const { return val >= addr_ && val < addr_ + len_; }
        mxtl::RefPtr<FifoDispatcher> fifo() const { return fifo_; }

    private:
        mx_vaddr_t addr_;
        size_t len_;
        mxtl::RefPtr<FifoDispatcher> fifo_;
    };
    using FifoTree = mxtl::WAVLTree<mx_vaddr_t, mxtl::unique_ptr<FifoRange>>;

    mutable mxtl::Mutex mutex;
    FifoTree fifos TA_GUARDED(mutex);

    status_t FindFifo(mx_vaddr_t addr, mxtl::RefPtr<FifoDispatcher>* fifo) const;
};
