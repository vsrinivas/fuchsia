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
#include <magenta/user_copy.h>
#include <magenta/user_thread.h>
#include <magenta/wait_set_dispatcher.h>

#include <mxalloc/new.h>
#include <mxtl/atomic.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr size_t kMaxCPRNGDraw = MX_CPRNG_DRAW_MAX_LEN;
constexpr size_t kMaxCPRNGSeed = MX_CPRNG_ADD_ENTROPY_MAX_LEN;

constexpr uint32_t kMaxWaitSetWaitResults = 1024u;

mx_status_t sys_nanosleep(mx_time_t deadline) {
    LTRACEF("nseconds %" PRIu64 "\n", deadline);

    if (deadline == 0ull) {
        thread_yield();
        return NO_ERROR;
    }

    // This syscall is declared as "blocking" in syscalls.sysgen, so a higher
    // layer will automatically retry if we return ERR_INTERRUPTED_RETRY.
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
        return ERR_ACCESS_DENIED;
    case MX_CLOCK_UTC:
        utc_offset.store(offset);
        return NO_ERROR;
    default:
        return ERR_INVALID_ARGS;
    }
}

mx_status_t sys_event_create(uint32_t options, user_ptr<mx_handle_t> _out) {
    LTRACEF("options 0x%x\n", options);

    if (options)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    status_t result = EventDispatcher::Create(options, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (_out.copy_to_user(up->MapHandleToValue(handle)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return NO_ERROR;
}

mx_status_t sys_eventpair_create(uint32_t options,
                                 user_ptr<mx_handle_t> _out0, user_ptr<mx_handle_t> _out1) {
    LTRACEF("entry out_handles %p,%p\n", _out0.get(), _out1.get());

    if (options != 0u)  // No options defined/supported yet.
        return ERR_NOT_SUPPORTED;

    mxtl::RefPtr<Dispatcher> epd0, epd1;
    mx_rights_t rights;
    status_t result = EventPairDispatcher::Create(&epd0, &epd1, &rights);
    if (result != NO_ERROR)
        return result;

    HandleOwner h0(MakeHandle(mxtl::move(epd0), rights));
    if (!h0)
        return ERR_NO_MEMORY;

    HandleOwner h1(MakeHandle(mxtl::move(epd1), rights));
    if (!h1)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    if (_out0.copy_to_user(up->MapHandleToValue(h0)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (_out1.copy_to_user(up->MapHandleToValue(h1)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    return NO_ERROR;
}

mx_status_t sys_log_create(uint32_t options, user_ptr<mx_handle_t> out) {
    LTRACEF("options 0x%x\n", options);

    // kernel option is forbidden to userspace
    options &= (~DLOG_FLAG_KERNEL);

    // create a Log dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = LogDispatcher::Create(options, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    // by default log objects are write-only
    // as readable logs are more expensive
    if (options & MX_LOG_FLAG_READABLE) {
        rights |= MX_RIGHT_READ;
    }

    // create a handle and attach the dispatcher to it
    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (out.copy_to_user(up->MapHandleToValue(handle)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));

    return NO_ERROR;
}

mx_status_t sys_log_write(mx_handle_t log_handle, uint32_t len, user_ptr<const void> _ptr, uint32_t options) {
    LTRACEF("log handle %d, len 0x%x, ptr 0x%p\n", log_handle, len, _ptr.get());

    if (len > DLOG_MAX_DATA)
        return ERR_OUT_OF_RANGE;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<LogDispatcher> log;
    mx_status_t status = up->GetDispatcherWithRights(log_handle, MX_RIGHT_WRITE, &log);
    if (status != NO_ERROR)
        return status;

    char buf[DLOG_MAX_RECORD];
    // TODO(andymutton): Change to use a user_ptr copy.
    if (magenta_copy_from_user(_ptr.get(), buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return log->Write(options, buf, len);
}

mx_status_t sys_log_read(mx_handle_t log_handle, uint32_t len, user_ptr<void> _ptr, uint32_t options) {
    LTRACEF("log handle %d, len 0x%x, ptr 0x%p\n", log_handle, len, _ptr.get());

    if (len < DLOG_MAX_RECORD)
        return ERR_BUFFER_TOO_SMALL;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<LogDispatcher> log;
    mx_status_t status = up->GetDispatcherWithRights(log_handle, MX_RIGHT_READ, &log);
    if (status != NO_ERROR)
        return status;

    char buf[DLOG_MAX_RECORD];
    size_t actual;
    if ((status = log->Read(options, buf, DLOG_MAX_RECORD, &actual)) < 0)
        return status;

    if (_ptr.copy_array_to_user(buf, actual) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return static_cast<mx_status_t>(actual);
}

mx_status_t sys_cprng_draw(user_ptr<void> _buffer, size_t len, user_ptr<size_t> _actual) {
    if (len > kMaxCPRNGDraw)
        return ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGDraw];

    auto prng = crypto::GlobalPRNG::GetInstance();
    ASSERT(prng->is_thread_safe());
    prng->Draw(kernel_buf, static_cast<int>(len));

    if (_buffer.copy_array_to_user(kernel_buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;
    if (_actual.copy_to_user(len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    // Get rid of the stack copy of the random data
    memset(kernel_buf, 0, sizeof(kernel_buf));

    return NO_ERROR;
}

mx_status_t sys_cprng_add_entropy(user_ptr<const void> _buffer, size_t len) {
    if (len > kMaxCPRNGSeed)
        return ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGSeed];
    if (_buffer.copy_array_from_user(kernel_buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    auto prng = crypto::GlobalPRNG::GetInstance();
    ASSERT(prng->is_thread_safe());
    prng->AddEntropy(kernel_buf, static_cast<int>(len));

    // Get rid of the stack copy of the random data
    memset(kernel_buf, 0, sizeof(kernel_buf));

    return NO_ERROR;
}

mx_status_t sys_waitset_create(uint32_t options, user_ptr<mx_handle_t> _out) {
    if (options != 0)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = WaitSetDispatcher::Create(&dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    if (_out.copy_to_user(up->MapHandleToValue(handle)) != NO_ERROR)
        return ERR_INVALID_ARGS;
    up->AddHandle(mxtl::move(handle));

    return NO_ERROR;
}

mx_status_t sys_waitset_add(mx_handle_t ws_handle_value,
                            uint64_t cookie,
                            mx_handle_t handle_value,
                            mx_signals_t signals) {
    LTRACEF("wait set handle %d, handle %d\n", ws_handle_value, handle_value);

    mxtl::unique_ptr<WaitSetDispatcher::Entry> entry;
    mx_status_t result = WaitSetDispatcher::Entry::Create(signals, cookie, &entry);
    if (result != NO_ERROR)
        return result;

    // TODO(vtl): Obviously, we need to get two handles under the handle table lock. We also call
    // WaitSetDispatcher::AddEntry() under it, which is quite terrible. However, it'd be quite
    // tricky to do it correctly otherwise.
    auto up = ProcessDispatcher::GetCurrent();
    AutoLock lock(up->handle_table_lock());

    Handle* ws_handle = up->GetHandleLocked(ws_handle_value);
    if (!ws_handle)
        return ERR_BAD_HANDLE;
    if (ws_handle->dispatcher()->get_type() != DispatchTag<WaitSetDispatcher>::ID)
        return ERR_WRONG_TYPE;
    // No need to take a ref to the dispatcher, since we're under the handle table lock. :-/
    auto ws_dispatcher = static_cast<WaitSetDispatcher*>(ws_handle->dispatcher().get());
    if (!magenta_rights_check(ws_handle, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    Handle* handle = up->GetHandleLocked(handle_value);
    if (!handle)
        return ERR_BAD_HANDLE;
    if (!magenta_rights_check(handle, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    return ws_dispatcher->AddEntry(mxtl::move(entry), handle);
}

mx_status_t sys_waitset_remove(mx_handle_t ws_handle, uint64_t cookie) {
    LTRACEF("wait set handle %d\n", ws_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<WaitSetDispatcher> ws_dispatcher;
    mx_status_t status =
        up->GetDispatcherWithRights(ws_handle, MX_RIGHT_WRITE, &ws_dispatcher);
    if (status != NO_ERROR)
        return status;

    return ws_dispatcher->RemoveEntry(cookie);
}

mx_status_t sys_waitset_wait(mx_handle_t ws_handle,
                             mx_time_t deadline,
                             user_ptr<mx_waitset_result_t> _results,
                             user_ptr<uint32_t> _count) {
    LTRACEF("wait set handle %d\n", ws_handle);

    uint32_t count;
    if (_count.copy_from_user(&count) != NO_ERROR)
        return ERR_INVALID_ARGS;

    //TODO: use inline array here
    mxtl::unique_ptr<mx_waitset_result_t[]> results;
    if (count > 0u) {
        if (count > kMaxWaitSetWaitResults)
            return ERR_OUT_OF_RANGE;

        // TODO(vtl): It kind of sucks that we always have to allocate the indicated maximum size
        // here (namely, |count|).
        AllocChecker ac;
        results.reset(new (&ac) mx_waitset_result_t[count]);
        if (!ac.check())
            return ERR_NO_MEMORY;
    }

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<WaitSetDispatcher> ws_dispatcher;
    mx_status_t status =
        up->GetDispatcherWithRights(ws_handle, MX_RIGHT_READ, &ws_dispatcher);
    if (status != NO_ERROR)
        return status;

    uint32_t max_results = 0u;
    mx_status_t result = ws_dispatcher->Wait(deadline, &count, results.get(), &max_results);
    if (result == NO_ERROR) {
        if (_count.copy_to_user(count) != NO_ERROR)
            return ERR_INVALID_ARGS;
        if (count > 0u) {
            if (_results.copy_array_to_user(results.get(), count) != NO_ERROR)
                return ERR_INVALID_ARGS;
        }
    }

    return result;
}
