// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/data_pipe.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <stddef.h>

#include <kernel/auto_lock.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <magenta/data_pipe_consumer_dispatcher.h>
#include <magenta/data_pipe_producer_dispatcher.h>
#include <magenta/handle.h>
#include <magenta/magenta.h>

const auto kDP_Map_Perms = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_USER;
const auto kDP_Map_Perms_RO = kDP_Map_Perms | ARCH_MMU_FLAG_PERM_READ;

mx_status_t DataPipe::Create(mx_size_t element_size,
                             mx_size_t capacity,
                             mxtl::RefPtr<Dispatcher>* producer,
                             mxtl::RefPtr<Dispatcher>* consumer,
                             mx_rights_t* producer_rights,
                             mx_rights_t* consumer_rights) {
    AllocChecker ac;
    mxtl::RefPtr<DataPipe> pipe = mxtl::AdoptRef(new (&ac) DataPipe(element_size, capacity));
    if (!ac.check())
        return ERR_NO_MEMORY;

    if (!pipe->Init())
        return ERR_NO_MEMORY;

    mx_status_t status;

    status = DataPipeProducerDispatcher::Create(pipe, producer, producer_rights);
    if (status != NO_ERROR)
        return status;

    status = DataPipeConsumerDispatcher::Create(pipe, consumer, consumer_rights);
    if (status != NO_ERROR)
        return status;

    return NO_ERROR;
}

DataPipe::DataPipe(mx_size_t element_size, mx_size_t capacity)
    : element_size_(element_size),
      capacity_(capacity),
      free_space_(0u) {
    producer_.state_tracker.set_initial_signals_state(
        mx_signals_state_t{MX_SIGNAL_WRITABLE, MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED});
    consumer_.state_tracker.set_initial_signals_state(
        mx_signals_state_t{0u, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED});

    consumer_.read_only = true;
}

DataPipe::~DataPipe() {
    DEBUG_ASSERT(!consumer_.alive);
    DEBUG_ASSERT(!producer_.alive);
}

bool DataPipe::Init() {
    // We limit the capacity because a vmo can be larger than representable with the mx_size_t type.
    if (capacity_ > kMaxDataPipeCapacity)
        return false;

    vmo_ = VmObject::Create(PMM_ALLOC_FLAG_ANY, ROUNDUP(capacity_, PAGE_SIZE));
    if (!vmo_)
        return false;
    DEBUG_ASSERT(vmo_->size() >= capacity_);

    free_space_ = capacity_;
    return true;
}

mx_size_t DataPipe::ComputeSize(mx_size_t from, mx_size_t to, mx_size_t requested) const {
    mx_size_t available = (from >= to) ? capacity_ - from : to - from;
    return available >= requested ? requested : available;
}

mx_status_t DataPipe::MapVMOIfNeededNoLock(EndPoint* ep, mxtl::RefPtr<VmAspace> aspace) {
    DEBUG_ASSERT(vmo_);

    if (ep->aspace && (ep->aspace != aspace)) {
        // We have been transfered to another process. Unmap and free.
        // TODO(cpu): Do this at a better time.
        ep->aspace->FreeRegion(reinterpret_cast<vaddr_t>(ep->vad_start));
        ep->aspace.reset();
    }

    // For large requests we can use demand page here instead of commit.
    auto perms = ep->read_only ? kDP_Map_Perms_RO : kDP_Map_Perms;
    auto status = aspace->MapObject(vmo_, "datapipe", 0u, capacity_,
                                    reinterpret_cast<void**>(&ep->vad_start), 0,
                                    VMM_FLAG_COMMIT, perms);
    if (status < 0)
        return status;

    ep->aspace = mxtl::move(aspace);
    return NO_ERROR;
}

void DataPipe::UpdateSignalsNoLock() {
    // TODO(vtl): Should be non-writable during a two-phase write and non-readable during a
    // two-phase read.
    if (free_space_ == 0u) {
        producer_.state_tracker.UpdateSatisfied(MX_SIGNAL_WRITABLE, 0u);
        consumer_.state_tracker.UpdateSatisfied(0u, MX_SIGNAL_READABLE);
    } else if (free_space_ == capacity_) {
        producer_.state_tracker.UpdateSatisfied(0u, MX_SIGNAL_WRITABLE);
        consumer_.state_tracker.UpdateState(MX_SIGNAL_READABLE, 0u,
                                            producer_.alive ? 0u : MX_SIGNAL_READABLE, 0u);
    } else {
        producer_.state_tracker.UpdateSatisfied(0u, MX_SIGNAL_WRITABLE);
        consumer_.state_tracker.UpdateSatisfied(0u, MX_SIGNAL_READABLE);
    }
}

mx_status_t DataPipe::ProducerWriteFromUser(const void* ptr, mx_size_t* requested) {
    AutoLock al(&lock_);

    // |expected| > 0 means there is a pending ProducerWriteBegin().
    if (producer_.expected)
        return ERR_BUSY;

    if (*requested % element_size_ != 0u)
        return ERR_INVALID_ARGS;

    if (!consumer_.alive)
        return ERR_REMOTE_CLOSED;

    if (*requested == 0u)
        return NO_ERROR;

    if (free_space_ == 0u)
        return ERR_SHOULD_WAIT;

    *requested = ComputeSize(producer_.cursor, consumer_.cursor, *requested);
    DEBUG_ASSERT(*requested % element_size_ == 0u);

    if (!ptr)
        return ERR_INVALID_ARGS;

    size_t written;
    status_t status = vmo_->WriteUser(ptr, producer_.cursor, *requested, &written);
    if (status < 0)
        return status;

    *requested = written;

    free_space_ -= written;
    producer_.cursor += written;

    if (producer_.cursor == capacity_)
        producer_.cursor = 0u;

    UpdateSignalsNoLock();

    return NO_ERROR;
}

mx_ssize_t DataPipe::ProducerWriteBegin(mxtl::RefPtr<VmAspace> aspace, void** ptr) {
    AutoLock al(&lock_);

    // |expected| > 0 means there is a pending ProducerWriteBegin().
    if (producer_.expected)
        return ERR_BUSY;

    if (!consumer_.alive)
        return ERR_REMOTE_CLOSED;

    if (free_space_ == 0u)
        return ERR_SHOULD_WAIT;

    auto status = MapVMOIfNeededNoLock(&producer_, mxtl::move(aspace));
    if (status < 0)
        return status;

    producer_.expected = ComputeSize(producer_.cursor, consumer_.cursor, capacity_);
    DEBUG_ASSERT(producer_.expected > 0u);
    DEBUG_ASSERT(producer_.expected % element_size_ == 0u);

    *ptr = producer_.vad_start + producer_.cursor;
    return static_cast<mx_ssize_t>(producer_.expected);
}

mx_status_t DataPipe::ProducerWriteEnd(mx_size_t written) {
    AutoLock al(&lock_);

    if (!producer_.expected)
        return ERR_BAD_STATE;

    if (written > producer_.expected || written % element_size_ != 0u) {
        // An invalid end-write still terminates the read.
        producer_.expected = 0u;
        UpdateSignalsNoLock();
        return ERR_INVALID_ARGS;
    }

    free_space_ -= written;
    producer_.cursor += written;
    producer_.expected = 0u;

    if (producer_.cursor == capacity_)
        producer_.cursor = 0u;

    UpdateSignalsNoLock();

    return NO_ERROR;
}

mx_status_t DataPipe::ConsumerReadFromUser(void* ptr,
                                           mx_size_t* requested,
                                           bool all_or_none,
                                           bool discard,
                                           bool peek) {
    DEBUG_ASSERT(!discard || !peek);

    AutoLock al(&lock_);

    // |expected| > 0 means there is a pending ConsumerReadBegin().
    if (consumer_.expected)
        return ERR_BUSY;

    if (*requested % element_size_ != 0u)
        return ERR_INVALID_ARGS;

    if (*requested == 0)
        return NO_ERROR;

    // TODO(vtl): Should probably return something else if |!producer_.alive|.
    if (free_space_ == capacity_)
        return ERR_SHOULD_WAIT;

    mx_size_t available = ComputeSize(consumer_.cursor, producer_.cursor, *requested);
    DEBUG_ASSERT(available % element_size_ == 0u);
    if (all_or_none && available != *requested)
        return ERR_OUT_OF_RANGE;
    *requested = available;

    if (!discard) {
        if (!ptr)
            return ERR_INVALID_ARGS;

        size_t read;
        status_t st = vmo_->ReadUser(ptr, consumer_.cursor, *requested, &read);
        if (st != NO_ERROR)
            return st;
        DEBUG_ASSERT(read == *requested);
        if (peek)
            return NO_ERROR;
    }

    free_space_ += *requested;
    consumer_.cursor += *requested;

    if (consumer_.cursor == capacity_)
        consumer_.cursor = 0u;

    UpdateSignalsNoLock();

    return NO_ERROR;
}

mx_ssize_t DataPipe::ConsumerQuery() {
    AutoLock al(&lock_);

    // |expected| > 0 means there is a pending ConsumerReadBegin().
    if (consumer_.expected)
        return ERR_BUSY;

    if (free_space_ == capacity_)
        return 0;

    mx_size_t available = ComputeSize(consumer_.cursor, producer_.cursor, capacity_);
    DEBUG_ASSERT(available % element_size_ == 0u);
    return static_cast<mx_ssize_t>(available);
}

mx_ssize_t DataPipe::ConsumerReadBegin(mxtl::RefPtr<VmAspace> aspace, void** ptr) {
    AutoLock al(&lock_);

    // |expected| > 0 means there is a pending ConsumerReadBegin().
    if (consumer_.expected)
        return ERR_BUSY;

    // TODO(vtl): Should probably return something else if |!producer_.alive|.
    if (free_space_ == capacity_)
        return ERR_SHOULD_WAIT;

    auto status = MapVMOIfNeededNoLock(&consumer_, mxtl::move(aspace));
    if (status < 0)
        return status;

    consumer_.expected = ComputeSize(consumer_.cursor, producer_.cursor, capacity_);
    DEBUG_ASSERT(consumer_.expected > 0u);
    DEBUG_ASSERT(consumer_.expected % element_size_ == 0u);

    *ptr = consumer_.vad_start + consumer_.cursor;
    return static_cast<mx_ssize_t>(consumer_.expected);
}

mx_status_t DataPipe::ConsumerReadEnd(mx_size_t read) {
    AutoLock al(&lock_);

    if (!consumer_.expected)
        return ERR_BAD_STATE;

    if (read > consumer_.expected || read % element_size_ != 0u) {
        // An invalid end-read still terminates the read.
        consumer_.expected = 0u;
        UpdateSignalsNoLock();
        return ERR_INVALID_ARGS;
    }

    free_space_ += read;
    consumer_.cursor += read;
    consumer_.expected = 0u;

    if (consumer_.cursor == capacity_)
        consumer_.cursor = 0u;

    UpdateSignalsNoLock();

    return NO_ERROR;
}

void DataPipe::OnProducerDestruction() {
    AutoLock al(&lock_);

    producer_.alive = false;

    if (producer_.aspace) {
        producer_.aspace->FreeRegion(reinterpret_cast<vaddr_t>(producer_.vad_start));
        producer_.aspace.reset();
    }

    if (consumer_.alive) {
        bool is_empty = (free_space_ == capacity_);
        consumer_.state_tracker.UpdateState(0u, MX_SIGNAL_PEER_CLOSED,
                                            is_empty ? MX_SIGNAL_READABLE : 0u, 0u);

        // We can drop the vmo since future reads are not going to succeed.
        if (is_empty)
            vmo_.reset();
    }
}

void DataPipe::OnConsumerDestruction() {
    AutoLock al(&lock_);

    consumer_.alive = false;
    vmo_.reset();

    if (consumer_.aspace) {
        consumer_.aspace->FreeRegion(reinterpret_cast<vaddr_t>(consumer_.vad_start));
        consumer_.aspace.reset();
    }

    if (producer_.alive) {
        producer_.state_tracker.UpdateState(MX_SIGNAL_WRITABLE, MX_SIGNAL_PEER_CLOSED,
                                            MX_SIGNAL_WRITABLE, 0u);
    }
}
