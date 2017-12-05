// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/fifo_dispatcher.h>

#include <string.h>

#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <object/handle.h>

using fbl::AutoLock;

// static
zx_status_t FifoDispatcher::Create(uint32_t count, uint32_t elemsize, uint32_t options,
                                   fbl::RefPtr<Dispatcher>* dispatcher0,
                                   fbl::RefPtr<Dispatcher>* dispatcher1,
                                   zx_rights_t* rights) {
    // count and elemsize must be nonzero
    // count must be a power of two
    // total size must be <= kMaxSizeBytes
    if (!count || !elemsize || (count & (count - 1)) ||
        (count > kMaxSizeBytes) || (elemsize > kMaxSizeBytes) ||
        ((count * elemsize) > kMaxSizeBytes)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    fbl::AllocChecker ac;
    auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<FifoDispatcher>());
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    auto holder1 = holder0;

    auto data0 = fbl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[count * elemsize]);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto fifo0 = fbl::AdoptRef(new (&ac) FifoDispatcher(fbl::move(holder0), options, count,
                                                        elemsize, fbl::move(data0)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto data1 = fbl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[count * elemsize]);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto fifo1 = fbl::AdoptRef(new (&ac) FifoDispatcher(fbl::move(holder1), options, count,
                                                        elemsize, fbl::move(data1)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    fifo0->Init(fifo1);
    fifo1->Init(fifo0);

    *rights = ZX_DEFAULT_FIFO_RIGHTS;
    *dispatcher0 = fbl::move(fifo0);
    *dispatcher1 = fbl::move(fifo1);
    return ZX_OK;
}

FifoDispatcher::FifoDispatcher(fbl::RefPtr<PeerHolder<FifoDispatcher>> holder,
                               uint32_t /*options*/, uint32_t count, uint32_t elem_size,
                               fbl::unique_ptr<uint8_t[]> data)
    : PeeredDispatcher(fbl::move(holder), ZX_FIFO_WRITABLE),
      elem_count_(count), elem_size_(elem_size), mask_(count - 1),
      peer_koid_(0u), head_(0u), tail_(0u), data_(fbl::move(data)) {
}

FifoDispatcher::~FifoDispatcher() {
}

// Thread safety analysis disabled as this happens during creation only,
// when no other thread could be accessing the object.
void FifoDispatcher::Init(fbl::RefPtr<FifoDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    other_ = fbl::move(other);
    peer_koid_ = other_->get_koid();
}

zx_status_t FifoDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if ((set_mask & ~ZX_USER_SIGNAL_ALL) || (clear_mask & ~ZX_USER_SIGNAL_ALL))
        return ZX_ERR_INVALID_ARGS;

    if (!peer) {
        UpdateState(clear_mask, set_mask);
        return ZX_OK;
    }

    fbl::RefPtr<FifoDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return ZX_ERR_PEER_CLOSED;
        other = other_;
    }

    return other->UserSignalSelf(clear_mask, set_mask);
}

zx_status_t FifoDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();
    UpdateState(clear_mask, set_mask);
    return ZX_OK;
}

void FifoDispatcher::on_zero_handles() {
    canary_.Assert();

    fbl::RefPtr<FifoDispatcher> fifo;
    {
        AutoLock lock(&lock_);
        fifo = fbl::move(other_);
    }
    if (fifo)
        fifo->OnPeerZeroHandles();
}

void FifoDispatcher::OnPeerZeroHandles() {
    canary_.Assert();

    AutoLock lock(&lock_);
    other_.reset();
    UpdateState(ZX_FIFO_WRITABLE, ZX_FIFO_PEER_CLOSED);
}

zx_status_t FifoDispatcher::WriteFromUser(user_in_ptr<const uint8_t> ptr, size_t len, uint32_t* actual) {
    canary_.Assert();

    fbl::RefPtr<FifoDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return ZX_ERR_PEER_CLOSED;
        other = other_;
    }

    return other->WriteSelf(ptr, len, actual);
}

zx_status_t FifoDispatcher::WriteSelf(user_in_ptr<const uint8_t> ptr, size_t bytelen, uint32_t* actual) {
    canary_.Assert();

    size_t count = bytelen / elem_size_;
    if (count == 0)
        return ZX_ERR_OUT_OF_RANGE;

    AutoLock lock(&lock_);

    uint32_t old_head = head_;

    // total number of available empty slots in the fifo
    size_t avail = elem_count_ - (head_ - tail_);

    if (avail == 0)
        return ZX_ERR_SHOULD_WAIT;

    bool was_empty = (avail == elem_count_);

    if (count > avail)
        count = avail;

    while (count > 0) {
        uint32_t offset = (head_ & mask_);

        // number of slots from target to end, inclusive
        uint32_t n = elem_count_ - offset;

        // number of slots we can actually copy
        size_t to_copy = (count > n) ? n : count;

        zx_status_t status = ptr.copy_array_from_user(&data_[offset * elem_size_],
                                                      to_copy * elem_size_);
        if (status != ZX_OK) {
            // roll back, in case this is the second copy
            head_ = old_head;
            return ZX_ERR_INVALID_ARGS;
        }

        // adjust head and count
        // due to size limitations on fifo, to_copy will always fit in a u32
        head_ += static_cast<uint32_t>(to_copy);
        count -= to_copy;
        ptr = ptr.byte_offset(to_copy * elem_size_);
    }

    // if was empty, we've become readable
    if (was_empty)
        UpdateState(0u, ZX_FIFO_READABLE);

    // if now full, we're no longer writable
    if (elem_count_ == (head_ - tail_))
        other_->UpdateState(ZX_FIFO_WRITABLE, 0u);

    *actual = (head_ - old_head);
    return ZX_OK;
}

zx_status_t FifoDispatcher::ReadToUser(user_out_ptr<uint8_t> ptr, size_t bytelen, uint32_t* actual) {
    canary_.Assert();

    size_t count = bytelen / elem_size_;
    if (count == 0)
        return ZX_ERR_OUT_OF_RANGE;

    AutoLock lock(&lock_);

    uint32_t old_tail = tail_;

    // total number of available entries to read from the fifo
    size_t avail = (head_ - tail_);

    if (avail == 0)
        return ZX_ERR_SHOULD_WAIT;

    bool was_full = (avail == elem_count_);

    if (count > avail)
        count = avail;

    while (count > 0) {
        uint32_t offset = (tail_ & mask_);

        // number of slots from target to end, inclusive
        uint32_t n = elem_count_ - offset;

        // number of slots we can actually copy
        size_t to_copy = (count > n) ? n : count;

        zx_status_t status = ptr.copy_array_to_user(&data_[offset * elem_size_],
                                                    to_copy * elem_size_);
        if (status != ZX_OK) {
            // roll back, in case this is the second copy
            tail_ = old_tail;
            return ZX_ERR_INVALID_ARGS;
        }

        // adjust tail and count
        // due to size limitations on fifo, to_copy will always fit in a u32
        tail_ += static_cast<uint32_t>(to_copy);
        count -= to_copy;
        ptr = ptr.byte_offset(to_copy * elem_size_);
    }

    // if we were full, we have become writable
    if (was_full && other_)
        other_->UpdateState(0u, ZX_FIFO_WRITABLE);

    // if we've become empty, we're no longer readable
    if ((head_ - tail_) == 0)
        UpdateState(ZX_FIFO_READABLE, 0u);

    *actual = (tail_ - old_tail);
    return ZX_OK;
}
