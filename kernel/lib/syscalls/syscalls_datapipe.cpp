// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <stdint.h>
#include <trace.h>

#include <lib/ktrace.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/data_pipe.h>
#include <magenta/data_pipe_consumer_dispatcher.h>
#include <magenta/data_pipe_producer_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr mx_size_t kDefaultDataPipeCapacity = 32 * 1024u;

// TODO(vtl): Do we want to provide the producer handle as an out parameter (possibly in the same
// way as in msgpipe_create, instead of overloading the return value)?
mx_handle_t sys_datapipe_create(uint32_t options, mx_size_t element_size, mx_size_t capacity,
                                user_ptr<mx_handle_t> _consumer_handle) {
    LTRACEF("options %u\n", options);

    if (!_consumer_handle)
        return ERR_INVALID_ARGS;

    if (element_size == 0u)
        return ERR_INVALID_ARGS;

    if (capacity % element_size != 0u)
        return ERR_INVALID_ARGS;

    if (!capacity) {
        capacity = kDefaultDataPipeCapacity - (kDefaultDataPipeCapacity % element_size);
        if (!capacity)
            capacity = element_size;
    }

    mxtl::RefPtr<Dispatcher> producer_dispatcher;
    mx_rights_t producer_rights;

    mxtl::RefPtr<Dispatcher> consumer_dispatcher;
    mx_rights_t consumer_rights;

    mx_status_t result = DataPipe::Create(element_size, capacity, &producer_dispatcher,
                                          &consumer_dispatcher, &producer_rights, &consumer_rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr producer_handle(MakeHandle(mxtl::move(producer_dispatcher), producer_rights));
    if (!producer_handle)
        return ERR_NO_MEMORY;

    HandleUniquePtr consumer_handle(MakeHandle(mxtl::move(consumer_dispatcher), consumer_rights));
    if (!consumer_handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t hv_producer = up->MapHandleToValue(producer_handle.get());
    mx_handle_t hv_consumer = up->MapHandleToValue(consumer_handle.get());

    if (_consumer_handle.copy_to_user(hv_consumer) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(producer_handle));
    up->AddHandle(mxtl::move(consumer_handle));

    return hv_producer;
}

mx_ssize_t sys_datapipe_write(mx_handle_t producer_handle, uint32_t flags, mx_size_t requested,
                              user_ptr<const void> _buffer) {
    LTRACEF("handle %d\n", producer_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<DataPipeProducerDispatcher> producer;
    mx_status_t status = up->GetDispatcher(producer_handle, &producer, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    if (flags & ~MX_DATAPIPE_WRITE_FLAG_MASK)
        return ERR_NOT_SUPPORTED;

    mx_size_t written = requested;
    status = producer->Write(_buffer, &written, flags & MX_DATAPIPE_WRITE_FLAG_ALL_OR_NONE);
    if (status < 0)
        return status;

    return written;
}

mx_ssize_t sys_datapipe_read(mx_handle_t consumer_handle, uint32_t flags, mx_size_t requested,
                             user_ptr<void> _buffer) {
    LTRACEF("handle %d\n", consumer_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<DataPipeConsumerDispatcher> consumer;
    mx_status_t status = up->GetDispatcher(consumer_handle, &consumer, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    if (flags & ~MX_DATAPIPE_READ_FLAG_MASK)
        return ERR_NOT_SUPPORTED;

    bool all_or_none = flags & MX_DATAPIPE_READ_FLAG_ALL_OR_NONE;
    bool discard = flags & MX_DATAPIPE_READ_FLAG_DISCARD;
    bool query = flags & MX_DATAPIPE_READ_FLAG_QUERY;
    bool peek = flags & MX_DATAPIPE_READ_FLAG_PEEK;
    if (query) {
        if (discard || peek)
            return ERR_INVALID_ARGS;
        // Note: We ignore "all or none".
        return consumer->Query();
    }
    if (discard && peek)
        return ERR_INVALID_ARGS;

    mx_size_t read = requested;
    status = consumer->Read(_buffer, &read, all_or_none, discard, peek);
    if (status < 0)
        return status;

    return read;
}

mx_ssize_t sys_datapipe_begin_write(mx_handle_t producer_handle, uint32_t flags,
                                    user_ptr<uintptr_t> _buffer) {
    LTRACEF("handle %d\n", producer_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<DataPipeProducerDispatcher> producer;
    mx_status_t status = up->GetDispatcher(producer_handle, &producer, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // Currently, no flags are supported with two-phase write.
    if (flags)
        return (flags & ~MX_DATAPIPE_WRITE_FLAG_MASK) ? ERR_NOT_SUPPORTED : ERR_INVALID_ARGS;

    uintptr_t user_addr = 0u;

    mx_ssize_t result = producer->BeginWrite(up->aspace(), reinterpret_cast<void**>(&user_addr));
    if (result < 0)
        return result;
    DEBUG_ASSERT(result > 0);

    if (_buffer.copy_to_user(user_addr) != NO_ERROR) {
        producer->EndWrite(0u);
        return ERR_INVALID_ARGS;
    }

    return result;
}

mx_status_t sys_datapipe_end_write(mx_handle_t producer_handle, mx_size_t written) {
    LTRACEF("handle %d\n", producer_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<DataPipeProducerDispatcher> producer;
    mx_status_t status = up->GetDispatcher(producer_handle, &producer, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return producer->EndWrite(written);
}

mx_ssize_t sys_datapipe_begin_read(mx_handle_t consumer_handle, uint32_t flags,
                                   user_ptr<uintptr_t> _buffer) {
    LTRACEF("handle %d\n", consumer_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<DataPipeConsumerDispatcher> consumer;
    mx_status_t status = up->GetDispatcher(consumer_handle, &consumer, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    // Currently, no flags are supported with two-phase read.
    if (flags)
        return (flags & ~MX_DATAPIPE_READ_FLAG_MASK) ? ERR_NOT_SUPPORTED : ERR_INVALID_ARGS;

    uintptr_t user_addr = 0u;

    mx_ssize_t result = consumer->BeginRead(up->aspace(), reinterpret_cast<void**>(&user_addr));
    if (result < 0)
        return result;
    DEBUG_ASSERT(result > 0);

    if (_buffer.copy_to_user(user_addr) != NO_ERROR) {
        consumer->EndRead(0u);
        return ERR_INVALID_ARGS;
    }

    return result;
}

mx_status_t sys_datapipe_end_read(mx_handle_t consumer_handle, mx_size_t read) {
    LTRACEF("handle %d\n", consumer_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<DataPipeConsumerDispatcher> consumer;
    mx_status_t status = up->GetDispatcher(consumer_handle, &consumer, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    return consumer->EndRead(read);
}
