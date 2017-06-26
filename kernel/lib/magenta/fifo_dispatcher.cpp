// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>

#include <kernel/auto_lock.h>
#include <lib/user_copy/user_ptr.h>
#include <magenta/fifo_dispatcher.h>
#include <magenta/handle.h>
#include <magenta/rights.h>
#include <mxalloc/new.h>

// static
status_t FifoDispatcher::Create(uint32_t count, uint32_t elemsize, uint32_t options,
                                mxtl::RefPtr<Dispatcher>* dispatcher0,
                                mxtl::RefPtr<Dispatcher>* dispatcher1,
                                mx_rights_t* rights) {
    // count and elemsize must be nonzero
    // count must be a power of two
    // total size must be <= kMaxSizeBytes
    if (!count || !elemsize || (count & (count - 1)) ||
        (count > kMaxSizeBytes) || (elemsize > kMaxSizeBytes) ||
        ((count * elemsize) > kMaxSizeBytes)) {
        return MX_ERR_OUT_OF_RANGE;
    }
    AllocChecker ac;
    auto fifo0 = mxtl::AdoptRef(new (&ac) FifoDispatcher(count, elemsize, options));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    auto fifo1 = mxtl::AdoptRef(new (&ac) FifoDispatcher(count, elemsize, options));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    mx_status_t status;
    if ((status = fifo0->Init(fifo1)) != MX_OK)
        return status;
    if ((status = fifo1->Init(fifo0)) != MX_OK)
        return status;

    *rights = MX_DEFAULT_FIFO_RIGHTS;
    *dispatcher0 = mxtl::RefPtr<Dispatcher>(fifo0.get());
    *dispatcher1 = mxtl::RefPtr<Dispatcher>(fifo1.get());
    return MX_OK;
}

FifoDispatcher::FifoDispatcher(uint32_t count, uint32_t elem_size, uint32_t /*options*/)
    : elem_count_(count), elem_size_(elem_size), mask_(count - 1),
      peer_koid_(0u), state_tracker_(MX_FIFO_WRITABLE),
      head_(0u), tail_(0u) {
}

FifoDispatcher::~FifoDispatcher() {
    free(data_);
}

// Thread safety analysis disabled as this happens during creation only,
// when no other thread could be accessing the object.
mx_status_t FifoDispatcher::Init(mxtl::RefPtr<FifoDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    other_ = mxtl::move(other);
    peer_koid_ = other_->get_koid();
    if ((data_ = (uint8_t*) calloc(elem_count_, elem_size_)) == nullptr)
        return MX_ERR_NO_MEMORY;
    return MX_OK;
}

mx_status_t FifoDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if ((set_mask & ~MX_USER_SIGNAL_ALL) || (clear_mask & ~MX_USER_SIGNAL_ALL))
        return MX_ERR_INVALID_ARGS;

    if (!peer) {
        state_tracker_.UpdateState(clear_mask, set_mask);
        return MX_OK;
    }

    mxtl::RefPtr<FifoDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return MX_ERR_PEER_CLOSED;
        other = other_;
    }

    return other->UserSignalSelf(clear_mask, set_mask);
}

mx_status_t FifoDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();
    state_tracker_.UpdateState(clear_mask, set_mask);
    return MX_OK;
}

void FifoDispatcher::on_zero_handles() {
    canary_.Assert();

    mxtl::RefPtr<FifoDispatcher> fifo;
    {
        AutoLock lock(&lock_);
        fifo = mxtl::move(other_);
    }
    if (fifo)
        fifo->OnPeerZeroHandles();
}

void FifoDispatcher::OnPeerZeroHandles() {
    canary_.Assert();

    AutoLock lock(&lock_);
    other_.reset();
    state_tracker_.UpdateState(MX_FIFO_WRITABLE, MX_FIFO_PEER_CLOSED);
}

mx_status_t FifoDispatcher::Write(const uint8_t* src, size_t len, uint32_t* actual) {
    auto copy_from_fn = [](const uint8_t* src, uint8_t* data, size_t len) -> mx_status_t {
        memcpy(data, src, len);
        return MX_OK;
    };
    return Write(src, len, actual, copy_from_fn);
}

mx_status_t FifoDispatcher::Read(uint8_t* dst, size_t len, uint32_t* actual) {
    auto copy_to_fn = [](uint8_t* dst, const uint8_t* data, size_t len) -> mx_status_t {
        memcpy(dst, data, len);
        return MX_OK;
    };
    return Read(dst, len, actual, copy_to_fn);
}

mx_status_t FifoDispatcher::WriteFromUser(const uint8_t* src, size_t len, uint32_t* actual) {
    auto copy_from_fn = [](const uint8_t* src, uint8_t* data, size_t len) -> mx_status_t {
        return make_user_ptr(src).copy_array_from_user(data, len);
    };
    return Write(src, len, actual, copy_from_fn);
}

mx_status_t FifoDispatcher::ReadToUser(uint8_t* dst, size_t len, uint32_t* actual) {
    auto copy_to_fn = [](uint8_t* dst, const uint8_t* data, size_t len) -> mx_status_t {
        return make_user_ptr(dst).copy_array_to_user(data, len);
    };
    return Read(dst, len, actual, copy_to_fn);
}

mx_status_t FifoDispatcher::Write(const uint8_t* ptr, size_t len, uint32_t* actual,
                                  fifo_copy_from_fn_t copy_from_fn) {
    canary_.Assert();

    mxtl::RefPtr<FifoDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return MX_ERR_PEER_CLOSED;
        other = other_;
    }

    return other->WriteSelf(ptr, len, actual, copy_from_fn);
}

mx_status_t FifoDispatcher::WriteSelf(const uint8_t* ptr, size_t bytelen, uint32_t* actual,
                                      fifo_copy_from_fn_t copy_from_fn) {
    canary_.Assert();

    size_t count = bytelen / elem_size_;
    if (count == 0)
        return MX_ERR_OUT_OF_RANGE;

    AutoLock lock(&lock_);

    uint32_t old_head = head_;

    // total number of available empty slots in the fifo
    size_t avail = elem_count_ - (head_ - tail_);

    if (avail == 0)
        return MX_ERR_SHOULD_WAIT;

    bool was_empty = (avail == elem_count_);

    if (count > avail)
        count = avail;

    while (count > 0) {
        uint32_t offset = (head_ & mask_);

        // number of slots from target to end, inclusive
        uint32_t n = elem_count_ - offset;

        // number of slots we can actually copy
        size_t to_copy = (count > n) ? n : count;

        mx_status_t status = copy_from_fn(ptr, data_ + offset * elem_size_, to_copy * elem_size_);
        if (status != MX_OK) {
            // roll back, in case this is the second copy
            head_ = old_head;
            return MX_ERR_INVALID_ARGS;
        }

        // adjust head and count
        // due to size limitations on fifo, to_copy will always fit in a u32
        head_ += static_cast<uint32_t>(to_copy);
        count -= to_copy;
        ptr += to_copy * elem_size_;
    }

    // if was empty, we've become readable
    if (was_empty)
        state_tracker_.UpdateState(0u, MX_FIFO_READABLE);

    // if now full, we're no longer writable
    if (elem_count_ == (head_ - tail_))
        other_->state_tracker_.UpdateState(MX_FIFO_WRITABLE, 0u);

    *actual = (head_ - old_head);
    return MX_OK;
}

mx_status_t FifoDispatcher::Read(uint8_t* ptr, size_t bytelen, uint32_t* actual,
                                 fifo_copy_to_fn_t copy_to_fn) {
    canary_.Assert();

    size_t count = bytelen / elem_size_;
    if (count == 0)
        return MX_ERR_OUT_OF_RANGE;

    AutoLock lock(&lock_);

    uint32_t old_tail = tail_;

    // total number of available entries to read from the fifo
    size_t avail = (head_ - tail_);

    if (avail == 0)
        return MX_ERR_SHOULD_WAIT;

    bool was_full = (avail == elem_count_);

    if (count > avail)
        count = avail;

    while (count > 0) {
        uint32_t offset = (tail_ & mask_);

        // number of slots from target to end, inclusive
        uint32_t n = elem_count_ - offset;

        // number of slots we can actually copy
        size_t to_copy = (count > n) ? n : count;

        mx_status_t status = copy_to_fn(ptr, data_ + offset * elem_size_, to_copy * elem_size_);
        if (status != MX_OK) {
            // roll back, in case this is the second copy
            tail_ = old_tail;
            return MX_ERR_INVALID_ARGS;
        }

        // adjust tail and count
        // due to size limitations on fifo, to_copy will always fit in a u32
        tail_ += static_cast<uint32_t>(to_copy);
        count -= to_copy;
        ptr += to_copy * elem_size_;

    }

    // if we were full, we have become writable
    if (was_full && other_)
        other_->state_tracker_.UpdateState(0u, MX_FIFO_WRITABLE);

    // if we've become empty, we're no longer readable
    if ((head_ - tail_) == 0)
        state_tracker_.UpdateState(MX_FIFO_READABLE, 0u);

    *actual = (tail_ - old_tail);
    return MX_OK;
}
