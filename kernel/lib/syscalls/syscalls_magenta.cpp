// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <new.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <arch/ops.h>

#include <kernel/auto_lock.h>
#include <kernel/mp.h>
#include <kernel/thread.h>

#include <lib/crypto/global_prng.h>
#include <lib/ktrace.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/event_dispatcher.h>
#include <magenta/event_pair_dispatcher.h>
#include <magenta/log_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/socket_dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/syscalls/log.h>
#include <magenta/user_copy.h>
#include <magenta/wait_set_dispatcher.h>

#include <mxtl/ref_ptr.h>
#include <mxtl/string_piece.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr mx_size_t kMaxCPRNGDraw = MX_CPRNG_DRAW_MAX_LEN;
constexpr mx_size_t kMaxCPRNGSeed = MX_CPRNG_ADD_ENTROPY_MAX_LEN;

constexpr uint32_t kMaxWaitSetWaitResults = 1024u;

mx_status_t sys_nanosleep(mx_time_t nanoseconds) {
    LTRACEF("nseconds %" PRIu64 "\n", nanoseconds);

    if (nanoseconds == 0ull) {
        thread_yield();
        return NO_ERROR;
    }

    return magenta_sleep(nanoseconds);
}

uint64_t sys_time_get(uint32_t clock_id) {
    switch (clock_id) {
    case MX_CLOCK_MONOTONIC:
        return current_time_hires();
    default:
        //TODO: figure out the best option here
        return 0u;
    }
}

mx_status_t sys_event_create(uint32_t options, user_ptr<mx_handle_t> out) {
    LTRACEF("options 0x%x\n", options);

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    status_t result = EventDispatcher::Create(options, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (out.copy_to_user(up->MapHandleToValue(handle.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return NO_ERROR;
}

mx_status_t sys_eventpair_create(uint32_t flags,
                                 user_ptr<mx_handle_t> out0, user_ptr<mx_handle_t> out1) {
    LTRACEF("entry out_handles %p,%p\n", out0.get(), out1.get());

    if (flags != 0u)  // No flags defined/supported yet.
        return ERR_NOT_SUPPORTED;

    mxtl::RefPtr<Dispatcher> epd0, epd1;
    mx_rights_t rights;
    status_t result = EventPairDispatcher::Create(&epd0, &epd1, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr h0(MakeHandle(mxtl::move(epd0), rights));
    if (!h0)
        return ERR_NO_MEMORY;

    HandleUniquePtr h1(MakeHandle(mxtl::move(epd1), rights));
    if (!h1)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    if (out0.copy_to_user(up->MapHandleToValue(h0.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (out1.copy_to_user(up->MapHandleToValue(h1.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    return NO_ERROR;
}

mx_status_t sys_futex_wait(user_ptr<mx_futex_t> value_ptr, int current_value, mx_time_t timeout) {
    return ProcessDispatcher::GetCurrent()->futex_context()->FutexWait(
        value_ptr, current_value, timeout);
}

mx_status_t sys_futex_wake(user_ptr<mx_futex_t> value_ptr, uint32_t count) {
    return ProcessDispatcher::GetCurrent()->futex_context()->FutexWake(value_ptr, count);
}

mx_status_t sys_futex_requeue(user_ptr<mx_futex_t> wake_ptr, uint32_t wake_count, int current_value,
                              user_ptr<mx_futex_t> requeue_ptr, uint32_t requeue_count) {
    return ProcessDispatcher::GetCurrent()->futex_context()->FutexRequeue(
        wake_ptr, wake_count, current_value, requeue_ptr, requeue_count);
}

int sys_log_create(uint32_t flags) {
    LTRACEF("flags 0x%x\n", flags);

    // kernel flag is forbidden to userspace
    flags &= (~DLOG_FLAG_KERNEL);

    // create a Log dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = LogDispatcher::Create(flags, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    // by default log objects are write-only
    // as readable logs are more expensive
    if (flags & MX_LOG_FLAG_READABLE) {
        rights |= MX_RIGHT_READ;
    }

    // create a handle and attach the dispatcher to it
    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(mxtl::move(handle));

    return hv;
}

int sys_log_write(mx_handle_t log_handle, uint32_t len, user_ptr<const void> ptr, uint32_t flags) {
    LTRACEF("log handle %d, len 0x%x, ptr 0x%p\n", log_handle, len, ptr.get());

    if (len > DLOG_MAX_ENTRY)
        return ERR_OUT_OF_RANGE;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<LogDispatcher> log;
    mx_status_t status = up->GetDispatcher(log_handle, &log, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    char buf[DLOG_MAX_ENTRY];
    if (magenta_copy_from_user(ptr.get(), buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return log->Write(buf, len, flags);
}

int sys_log_read(mx_handle_t log_handle, uint32_t len, user_ptr<void> ptr, uint32_t flags) {
    LTRACEF("log handle %d, len 0x%x, ptr 0x%p\n", log_handle, len, ptr.get());

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<LogDispatcher> log;
    mx_status_t status = up->GetDispatcher(log_handle, &log, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    return log->ReadFromUser(ptr.get(), len, flags);
}

mx_status_t sys_cprng_draw(user_ptr<void> buffer, mx_size_t len, user_ptr<mx_size_t> actual) {
    if (len > kMaxCPRNGDraw)
        return ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGDraw];

    auto prng = crypto::GlobalPRNG::GetInstance();
    prng->Draw(kernel_buf, static_cast<int>(len));

    if (buffer.copy_array_to_user(kernel_buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;
    if (actual.copy_to_user(len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    // Get rid of the stack copy of the random data
    memset(kernel_buf, 0, sizeof(kernel_buf));

    return NO_ERROR;
}

mx_status_t sys_cprng_add_entropy(user_ptr<const void> buffer, mx_size_t len) {
    if (len > kMaxCPRNGSeed)
        return ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGSeed];
    if (buffer.copy_array_from_user(kernel_buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    auto prng = crypto::GlobalPRNG::GetInstance();
    prng->AddEntropy(kernel_buf, static_cast<int>(len));

    // Get rid of the stack copy of the random data
    memset(kernel_buf, 0, sizeof(kernel_buf));

    return NO_ERROR;
}

mx_status_t sys_waitset_create(uint32_t options, user_ptr<mx_handle_t> out) {
    if (options != 0)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = WaitSetDispatcher::Create(&dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    if (out.copy_to_user(up->MapHandleToValue(handle.get())) != NO_ERROR)
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

    Handle* ws_handle = up->GetHandle_NoLock(ws_handle_value);
    if (!ws_handle)
        return up->BadHandle(ws_handle_value, ERR_BAD_HANDLE);
    // No need to take a ref to the dispatcher, since we're under the handle table lock. :-/
    auto ws_dispatcher = ws_handle->dispatcher()->get_specific<WaitSetDispatcher>();
    if (!ws_dispatcher)
        return up->BadHandle(ws_handle_value, ERR_WRONG_TYPE);
    if (!magenta_rights_check(ws_handle->rights(), MX_RIGHT_WRITE))
        return up->BadHandle(ws_handle_value, ERR_ACCESS_DENIED);

    Handle* handle = up->GetHandle_NoLock(handle_value);
    if (!handle)
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);
    if (!magenta_rights_check(handle->rights(), MX_RIGHT_READ))
        return up->BadHandle(handle_value, ERR_ACCESS_DENIED);

    return ws_dispatcher->AddEntry(mxtl::move(entry), handle);
}

mx_status_t sys_waitset_remove(mx_handle_t ws_handle, uint64_t cookie) {
    LTRACEF("wait set handle %d\n", ws_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<WaitSetDispatcher> ws_dispatcher;
    mx_status_t status =
        up->GetDispatcher(ws_handle, &ws_dispatcher, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return ws_dispatcher->RemoveEntry(cookie);
}

mx_status_t sys_waitset_wait(mx_handle_t ws_handle,
                             mx_time_t timeout,
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
        up->GetDispatcher(ws_handle, &ws_dispatcher, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    uint32_t max_results = 0u;
    mx_status_t result = ws_dispatcher->Wait(timeout, &count, results.get(), &max_results);
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

mx_status_t sys_socket_create(uint32_t flags, user_ptr<mx_handle_t> out0, user_ptr<mx_handle_t> out1) {
    LTRACEF("entry out_handles %p, %p\n", out0.get(), out1.get());

    if (flags != 0u)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> socket0, socket1;
    mx_rights_t rights;
    status_t result = SocketDispatcher::Create(flags, &socket0, &socket1, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr h0(MakeHandle(mxtl::move(socket0), rights));
    if (!h0)
        return ERR_NO_MEMORY;

    HandleUniquePtr h1(MakeHandle(mxtl::move(socket1), rights));
    if (!h1)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (out0.copy_to_user(up->MapHandleToValue(h0.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (out1.copy_to_user(up->MapHandleToValue(h1.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    return NO_ERROR;
}

mx_status_t sys_socket_write(mx_handle_t handle, uint32_t flags,
                             user_ptr<const void> _buffer, mx_size_t size,
                             user_ptr<mx_size_t> actual) {
    LTRACEF("handle %d\n", handle);

    if ((size > 0u) && !_buffer)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<SocketDispatcher> socket;
    mx_status_t status = up->GetDispatcher(handle, &socket, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    switch (flags) {
    case 0: {
        mx_ssize_t result = socket->Write(_buffer.get(), size, true);

        if (result < 0)
            return static_cast<mx_status_t>(result);

        // caller may ignore results if desired
        if (actual) {
            if (actual.copy_to_user(static_cast<mx_size_t>(result)) != NO_ERROR)
                return ERR_INVALID_ARGS;
        }

        return NO_ERROR;
    }
    case MX_SOCKET_HALF_CLOSE:
        if (size == 0)
            return socket->HalfClose();
    // fall thru if size != 0.
    default:
        return ERR_INVALID_ARGS;
    }
}

mx_status_t sys_socket_read(mx_handle_t handle, uint32_t flags,
                            user_ptr<void> _buffer, mx_size_t size,
                            user_ptr<mx_size_t> actual) {
    LTRACEF("handle %d\n", handle);

    if (!_buffer || !actual || flags)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<SocketDispatcher> socket;
    mx_status_t status = up->GetDispatcher(handle, &socket, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    mx_ssize_t result = socket->Read(_buffer.get(), size, true);

    if (result < 0)
        return static_cast<mx_status_t>(result);

    if (actual.copy_to_user(static_cast<mx_size_t>(result)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}
