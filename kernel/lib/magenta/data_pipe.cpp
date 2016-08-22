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

mx_status_t DataPipe::Create(mx_size_t capacity,
                             utils::RefPtr<Dispatcher>* producer,
                             utils::RefPtr<Dispatcher>* consumer,
                             mx_rights_t* producer_rights,
                             mx_rights_t* consumer_rights) {
    AllocChecker ac;
    utils::RefPtr<DataPipe> pipe = utils::AdoptRef(new (&ac) DataPipe(capacity));
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

DataPipe::DataPipe(mx_size_t capacity)
    : capacity_(capacity),
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
    // We limit the capacity because a vmo can be larger than representable
    // with the mx_size_t type.
    if (capacity_ > kMaxDataPipeCapacity)
        return false;

    vmo_ = VmObject::Create(PMM_ALLOC_FLAG_ANY, ROUNDUP(capacity_, PAGE_SIZE));
    if (!vmo_)
        return false;

    free_space_ = static_cast<mx_size_t>(vmo_->size());
    return true;
}

mx_size_t DataPipe::ComputeSize(mx_size_t from, mx_size_t to, mx_size_t requested) {
    mx_size_t available;
    if (from >= to) {
        available = static_cast<mx_size_t>(vmo_->size() - from);
    } else {
        available = static_cast<mx_size_t>(to - from);
    }
    return available >= requested ? requested : available;
}

mx_status_t DataPipe::MapVMOIfNeeded(EndPoint* ep, utils::RefPtr<VmAspace> aspace) {
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

    ep->aspace = utils::move(aspace);
    return NO_ERROR;
}

void DataPipe::UpdateSignals() {
    if (free_space_ == 0u) {
        producer_.state_tracker.UpdateSatisfied(MX_SIGNAL_WRITABLE, 0u);
        consumer_.state_tracker.UpdateSatisfied(0u, MX_SIGNAL_READABLE);
    } else if (free_space_ == vmo_->size()) {
        producer_.state_tracker.UpdateSatisfied(0u, MX_SIGNAL_WRITABLE);
        consumer_.state_tracker.UpdateState(MX_SIGNAL_READABLE, 0u,
                                            producer_.alive ? 0u : MX_SIGNAL_READABLE, 0u);
    } else {
        producer_.state_tracker.UpdateSatisfied(0u, MX_SIGNAL_WRITABLE);
        consumer_.state_tracker.UpdateSatisfied(0u, MX_SIGNAL_READABLE);
    }
}

mx_status_t DataPipe::ProducerWriteFromUser(const void* ptr, mx_size_t* requested) {
    if (*requested == 0)
        return ERR_INVALID_ARGS;

    AutoLock al(&lock_);
    // |expected| > 0 means there is a pending ProducerWriteBegin().
    if (producer_.expected)
        return ERR_BUSY; // MOJO_RESULT_BUSY

    if (!consumer_.alive)
        return ERR_CHANNEL_CLOSED; // MOJO_RESULT_FAILED_PRECONDITION

    if (free_space_ == 0u)
        return ERR_NOT_READY; // MOJO_RESULT_SHOULD_WAIT

    *requested = ComputeSize(producer_.cursor, consumer_.cursor, *requested);

    size_t written;
    status_t status = vmo_->WriteUser(ptr, producer_.cursor, *requested, &written);
    if (status < 0)
        return status;

    *requested = written;

    free_space_ -= written;
    producer_.cursor += written;

    if (producer_.cursor == vmo_->size())
        producer_.cursor = 0u;

    UpdateSignals();

    return NO_ERROR;
}

mx_status_t DataPipe::ProducerWriteBegin(utils::RefPtr<VmAspace> aspace,
                                         void** ptr, mx_size_t* requested) {
    if (*requested == 0)
        return ERR_INVALID_ARGS;

    AutoLock al(&lock_);
    // |expected| > 0 means there is a pending ProducerWriteBegin().
    if (producer_.expected)
        return ERR_BUSY; // MOJO_RESULT_BUSY

    if (!consumer_.alive)
        return ERR_CHANNEL_CLOSED; // MOJO_RESULT_FAILED_PRECONDITION

    if (free_space_ == 0u)
        return ERR_NOT_READY; // MOJO_RESULT_SHOULD_WAIT

    auto status = MapVMOIfNeeded(&producer_, utils::move(aspace));
    if (status < 0)
        return status;

    *requested = ComputeSize(producer_.cursor, consumer_.cursor, *requested);

    producer_.expected = *requested;

    *ptr = producer_.vad_start + producer_.cursor;
    return NO_ERROR;
}

mx_status_t DataPipe::ProducerWriteEnd(mx_size_t written) {
    AutoLock al(&lock_);

    if (!producer_.expected)
        return ERR_BAD_STATE;

    if (written > producer_.expected)
        return ERR_INVALID_ARGS;

    free_space_ -= written;
    producer_.cursor += written;
    producer_.expected = 0u;

    if (producer_.cursor == vmo_->size())
        producer_.cursor = 0u;

    UpdateSignals();

    return NO_ERROR;
}

mx_status_t DataPipe::ConsumerReadFromUser(void* ptr, mx_size_t* requested) {
    if (*requested == 0)
        return ERR_INVALID_ARGS;

    AutoLock al(&lock_);
    // |expected| > 0 means there is a pending ConsumerReadBegin().
    if (consumer_.expected)
        return ERR_BUSY; // MOJO_RESULT_BUSY

    if (free_space_ == vmo_->size())
        return ERR_NOT_READY; // MOJO_RESULT_SHOULD_WAIT

    *requested = ComputeSize(consumer_.cursor, producer_.cursor, *requested);

    size_t read;
    status_t st = vmo_->ReadUser(ptr, consumer_.cursor, *requested, &read);
    if (st < 0)
        return st;

    *requested = read;

    free_space_ += read;
    consumer_.cursor += read;

    if (consumer_.cursor == vmo_->size())
        consumer_.cursor = 0u;

    UpdateSignals();

    return NO_ERROR;
}

mx_status_t DataPipe::ConsumerReadBegin(utils::RefPtr<VmAspace> aspace,
                                        void** ptr, mx_size_t* requested) {
    if (*requested == 0)
        return ERR_INVALID_ARGS;

    AutoLock al(&lock_);

    // |expected| > 0 means there is a pending ConsumerReadBegin().
    if (consumer_.expected)
        return ERR_BUSY; // MOJO_RESULT_BUSY

    if (free_space_ == vmo_->size())
        return ERR_NOT_READY; // MOJO_RESULT_SHOULD_WAIT

    auto status = MapVMOIfNeeded(&consumer_, utils::move(aspace));
    if (status < 0)
        return status;

    *requested = ComputeSize(consumer_.cursor, producer_.cursor, *requested);

    consumer_.expected = *requested;

    *ptr = consumer_.vad_start + consumer_.cursor;
    return NO_ERROR;
}

mx_status_t DataPipe::ConsumerReadEnd(mx_size_t read) {
    AutoLock al(&lock_);

    if (!consumer_.expected)
        return ERR_BAD_STATE;

    if (read > consumer_.expected)
        return ERR_INVALID_ARGS;

    free_space_ += read;
    consumer_.cursor += read;
    consumer_.expected = 0u;

    if (consumer_.cursor == vmo_->size())
        consumer_.cursor = 0u;

    UpdateSignals();

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
        bool is_empty = (free_space_ == vmo_->size());
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
