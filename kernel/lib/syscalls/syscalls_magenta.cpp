// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>

#include <explicit-memory/bytes.h>
#include <lib/crypto/global_prng.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/event_dispatcher.h>
#include <magenta/event_pair_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/log_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/log.h>
#include <magenta/syscalls/policy.h>
#include <magenta/user_copy.h>
#include <magenta/user_thread.h>

#include <mxalloc/new.h>
#include <mxtl/atomic.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr size_t kMaxCPRNGDraw = MX_CPRNG_DRAW_MAX_LEN;
constexpr size_t kMaxCPRNGSeed = MX_CPRNG_ADD_ENTROPY_MAX_LEN;

mx_status_t sys_nanosleep(mx_time_t deadline) {
    LTRACEF("nseconds %" PRIu64 "\n", deadline);

    if (deadline == 0ull) {
        thread_yield();
        return MX_OK;
    }

    // This syscall is declared as "blocking" in syscalls.sysgen, so a higher
    // layer will automatically retry if we return MX_ERR_INTERNAL_INTR_RETRY.
    return magenta_sleep(deadline);
}

// This must be accessed atomically from any given thread.
static mxtl::atomic<int64_t> utc_offset;

uint64_t sys_time_get(uint32_t clock_id) {
    switch (clock_id) {
    case MX_CLOCK_MONOTONIC:
        return current_time();
    case MX_CLOCK_UTC:
        return current_time() + utc_offset.load();
    case MX_CLOCK_THREAD:
        return UserThread::GetCurrent()->runtime_ns();
    default:
        //TODO: figure out the best option here
        return 0u;
    }
}

mx_status_t sys_clock_adjust(mx_handle_t hrsrc, uint32_t clock_id, int64_t offset) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(hrsrc)) < 0) {
        return status;
    }

    switch (clock_id) {
    case MX_CLOCK_MONOTONIC:
        return MX_ERR_ACCESS_DENIED;
    case MX_CLOCK_UTC:
        utc_offset.store(offset);
        return MX_OK;
    default:
        return MX_ERR_INVALID_ARGS;
    }
}

mx_status_t sys_event_create(uint32_t options, user_ptr<mx_handle_t> _out) {
    LTRACEF("options 0x%x\n", options);

    if (options)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mx_status_t res = up->QueryPolicy(MX_POL_NEW_EVENT);
    if (res < 0)
        return res;


    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    status_t result = EventDispatcher::Create(options, &dispatcher, &rights);
    if (result != MX_OK)
        return result;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    if (_out.copy_to_user(up->MapHandleToValue(handle)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return MX_OK;
}

mx_status_t sys_eventpair_create(uint32_t options,
                                 user_ptr<mx_handle_t> _out0, user_ptr<mx_handle_t> _out1) {
    LTRACEF("entry out_handles %p,%p\n", _out0.get(), _out1.get());

    if (options != 0u)  // No options defined/supported yet.
        return MX_ERR_NOT_SUPPORTED;

    auto up = ProcessDispatcher::GetCurrent();
    mx_status_t res = up->QueryPolicy(MX_POL_NEW_EVPAIR);
    if (res < 0)
        return res;

    mxtl::RefPtr<Dispatcher> epd0, epd1;
    mx_rights_t rights;
    status_t result = EventPairDispatcher::Create(&epd0, &epd1, &rights);
    if (result != MX_OK)
        return result;

    HandleOwner h0(MakeHandle(mxtl::move(epd0), rights));
    if (!h0)
        return MX_ERR_NO_MEMORY;

    HandleOwner h1(MakeHandle(mxtl::move(epd1), rights));
    if (!h1)
        return MX_ERR_NO_MEMORY;

    if (_out0.copy_to_user(up->MapHandleToValue(h0)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    if (_out1.copy_to_user(up->MapHandleToValue(h1)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    return MX_OK;
}

mx_status_t sys_log_create(uint32_t options, user_ptr<mx_handle_t> out) {
    LTRACEF("options 0x%x\n", options);

    // kernel option is forbidden to userspace
    options &= (~DLOG_FLAG_KERNEL);

    // create a Log dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = LogDispatcher::Create(options, &dispatcher, &rights);
    if (result != MX_OK)
        return result;

    // by default log objects are write-only
    // as readable logs are more expensive
    if (options & MX_LOG_FLAG_READABLE) {
        rights |= MX_RIGHT_READ;
    }

    // create a handle and attach the dispatcher to it
    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (out.copy_to_user(up->MapHandleToValue(handle)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));

    return MX_OK;
}

mx_status_t sys_log_write(mx_handle_t log_handle, uint32_t len, user_ptr<const void> _ptr, uint32_t options) {
    LTRACEF("log handle %d, len 0x%x, ptr 0x%p\n", log_handle, len, _ptr.get());

    if (len > DLOG_MAX_DATA)
        return MX_ERR_OUT_OF_RANGE;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<LogDispatcher> log;
    mx_status_t status = up->GetDispatcherWithRights(log_handle, MX_RIGHT_WRITE, &log);
    if (status != MX_OK)
        return status;

    char buf[DLOG_MAX_RECORD];
    // TODO(andymutton): Change to use a user_ptr copy.
    if (magenta_copy_from_user(_ptr.get(), buf, len) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return log->Write(options, buf, len);
}

mx_status_t sys_log_read(mx_handle_t log_handle, uint32_t len, user_ptr<void> _ptr, uint32_t options) {
    LTRACEF("log handle %d, len 0x%x, ptr 0x%p\n", log_handle, len, _ptr.get());

    if (len < DLOG_MAX_RECORD)
        return MX_ERR_BUFFER_TOO_SMALL;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<LogDispatcher> log;
    mx_status_t status = up->GetDispatcherWithRights(log_handle, MX_RIGHT_READ, &log);
    if (status != MX_OK)
        return status;

    char buf[DLOG_MAX_RECORD];
    size_t actual;
    if ((status = log->Read(options, buf, DLOG_MAX_RECORD, &actual)) < 0)
        return status;

    if (_ptr.copy_array_to_user(buf, actual) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return static_cast<mx_status_t>(actual);
}

mx_status_t sys_cprng_draw(user_ptr<void> _buffer, size_t len, user_ptr<size_t> _actual) {
    if (len > kMaxCPRNGDraw)
        return MX_ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGDraw];
    // Ensure we get rid of the stack copy of the random data as this function
    // returns.
    explicit_memory::ZeroDtor<uint8_t> zero_guard(kernel_buf, sizeof(kernel_buf));

    auto prng = crypto::GlobalPRNG::GetInstance();
    ASSERT(prng->is_thread_safe());
    prng->Draw(kernel_buf, static_cast<int>(len));

    if (_buffer.copy_array_to_user(kernel_buf, len) != MX_OK)
        return MX_ERR_INVALID_ARGS;
    if (_actual.copy_to_user(len) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return MX_OK;
}

mx_status_t sys_cprng_add_entropy(user_ptr<const void> _buffer, size_t len) {
    if (len > kMaxCPRNGSeed)
        return MX_ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGSeed];
    // Ensure we get rid of the stack copy of the entropy as this function
    // returns.
    explicit_memory::ZeroDtor<uint8_t> zero_guard(kernel_buf, sizeof(kernel_buf));

    if (_buffer.copy_array_from_user(kernel_buf, len) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    auto prng = crypto::GlobalPRNG::GetInstance();
    ASSERT(prng->is_thread_safe());
    prng->AddEntropy(kernel_buf, static_cast<int>(len));

    return MX_OK;
}
