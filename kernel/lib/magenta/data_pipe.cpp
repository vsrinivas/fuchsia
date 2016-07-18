// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/data_pipe.h>

#include <err.h>
#include <stddef.h>

#include <kernel/auto_lock.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_aspace.h>

#include <magenta/handle.h>
#include <magenta/magenta.h>
#include <magenta/data_pipe_producer_dispatcher.h>
#include <magenta/data_pipe_consumer_dispatcher.h>

const auto kMMU_Map_Perms = ARCH_MMU_FLAG_PERM_NO_EXECUTE | ARCH_MMU_FLAG_PERM_USER;


mx_status_t DataPipe::Create(mx_size_t capacity,
                             utils::RefPtr<Dispatcher>* producer,
                             utils::RefPtr<Dispatcher>* consumer,
                             mx_rights_t* producer_rights,
                             mx_rights_t* consumer_rights) {
    utils::RefPtr<DataPipe> pipe = utils::AdoptRef(new DataPipe(capacity));
    if (!pipe)
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
      available_(0u) {
    mutex_init(&lock_);
    producer_.waiter.set_initial_signals_state(
            mx_signals_state_t{MX_SIGNAL_WRITABLE, MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED});
    consumer_.waiter.set_initial_signals_state(
            mx_signals_state_t{0u, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED});
}

DataPipe::~DataPipe() {
    mutex_destroy(&lock_);
}

Waiter* DataPipe::get_producer_waiter() {
    return &producer_.waiter;
}

Waiter* DataPipe::get_consumer_waiter() {
    return &consumer_.waiter;
}

bool DataPipe::Init() {
    vmo_ = VmObject::Create(PMM_ALLOC_FLAG_ANY, ROUNDUP(capacity_, PAGE_SIZE));
    if (!vmo_)
        return false;

    available_ = vmo_->size();
    return true;
}

mx_status_t DataPipe::ProducerWriteBegin(utils::RefPtr<VmAspace> aspace,
                                         void** ptr, mx_size_t* requested) {
    AutoLock al(&lock_);

    if (producer_.aspace)
        return ERR_BUSY;  // MOJO_RESULT_BUSY

    if (!consumer_.alive)
        return ERR_CHANNEL_CLOSED;  // MOJO_RESULT_FAILED_PRECONDITION

    if (!available_)
        return ERR_NOT_READY;    // MOJO_RESULT_SHOULD_WAIT

    auto future_cursor = producer_.cursor + *requested;

    if (producer_.cursor > consumer_.cursor) {
        if (future_cursor > vmo_->size())
            *requested = static_cast<mx_size_t>(vmo_->size() - producer_.cursor);
    } else if (producer_.cursor < consumer_.cursor) {
        if (future_cursor > consumer_.cursor)
            *requested = static_cast<mx_size_t>(consumer_.cursor - producer_.cursor);
    }

    size_t start = ROUNDDOWN(producer_.cursor, PAGE_SIZE);
    size_t offset = producer_.cursor - start;
    size_t length = ROUNDUP(*requested + offset, PAGE_SIZE);

    // For large requests we can use demand page here instead of commit.
    auto status = aspace->MapObject(vmo_, "datap_prod", start, length, ptr, 0,
                                    VMM_FLAG_COMMIT, kMMU_Map_Perms);
    if (status < 0)
        return status;

    producer_.vad_start = reinterpret_cast<vaddr_t>(*ptr);
    producer_.max_size = *requested;
    producer_.aspace = utils::move(aspace);

    *ptr =  reinterpret_cast<char*>(*ptr) + offset;
    return NO_ERROR;
}

mx_status_t DataPipe::ProducerWriteEnd(mx_size_t written) {
    AutoLock al(&lock_);

    if (!producer_.aspace)
        return ERR_BAD_STATE;

    if (written > producer_.max_size)
        return ERR_INVALID_ARGS;

    auto status = producer_.aspace->FreeRegion(producer_.vad_start);
    if (status < 0)
        return status;

    producer_.aspace.reset();
    producer_.vad_start = 0u;
    producer_.cursor += written;

    available_ -= written;

    if (producer_.cursor == vmo_->size())
        producer_.cursor = 0u;

    return NO_ERROR;
}

mx_status_t DataPipe::ConsumerReadBegin(utils::RefPtr<VmAspace> aspace,
                                        void** ptr, mx_size_t* requested) {
    AutoLock al(&lock_);

    if (consumer_.aspace)
        return ERR_BUSY;  // MOJO_RESULT_BUSY

    if (available_ == vmo_->size())
        return ERR_NOT_READY;  // MOJO_RESULT_SHOULD_WAIT

    auto future_cursor = consumer_.cursor + *requested;

    if (consumer_.cursor < producer_.cursor) {
        if (future_cursor > producer_.cursor)
            *requested = static_cast<mx_size_t>(producer_.cursor - consumer_.cursor);
    } else if (consumer_.cursor > producer_.cursor) {
        if (future_cursor > vmo_->size())
            *requested = static_cast<mx_size_t>(vmo_->size() - consumer_.cursor);
    }

    size_t start = ROUNDDOWN(consumer_.cursor, PAGE_SIZE);
    size_t offset = consumer_.cursor - start;
    size_t length = ROUNDUP(*requested + offset, PAGE_SIZE);

    // For large requests we can use demand page here instead of commit.
    auto status = aspace->MapObject(vmo_, "datap_cons", start, length, ptr, 0,
                                    VMM_FLAG_COMMIT, kMMU_Map_Perms | ARCH_MMU_FLAG_PERM_RO);
    if (status < 0)
        return status;

    consumer_.vad_start = reinterpret_cast<vaddr_t>(*ptr);
    consumer_.max_size = *requested;
    consumer_.aspace = utils::move(aspace);

    *ptr =  reinterpret_cast<char*>(*ptr) + offset;
    return NO_ERROR;

}

mx_status_t DataPipe::ConsumerReadEnd(mx_size_t read) {
    AutoLock al(&lock_);

    if (!consumer_.aspace)
        return ERR_BAD_STATE;

    if (read > producer_.max_size)
        return ERR_INVALID_ARGS;

    auto status = consumer_.aspace->FreeRegion(consumer_.vad_start);
    if (status < 0)
        return status;

    consumer_.aspace.reset();
    consumer_.vad_start = 0u;
    consumer_.cursor += read;

    available_ += read;

    if (consumer_.cursor == vmo_->size())
        consumer_.cursor = 0u;

    return NO_ERROR;
}

void DataPipe::OnProducerDestruction() {
    AutoLock al(&lock_);
    if (producer_.aspace) {
        mutex_release(&lock_);
        ProducerWriteEnd(0u);
        mutex_acquire(&lock_);
    }
    producer_.alive = false;
}

void DataPipe::OnConsumerDestruction() {
    AutoLock al(&lock_);
    if (consumer_.aspace) {
        mutex_release(&lock_);
        ConsumerReadEnd(0u);
        mutex_acquire(&lock_);
    }
    consumer_.alive = false;
}
