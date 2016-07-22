// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <arch/ops.h>
#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/mp.h>
#include <kernel/vm/vm_region.h>
#include <kernel/vm/vm_object.h>
#include <lib/console.h>
#include <lib/crypto/global_prng.h>
#include <lib/user_copy.h>
#include <list.h>

#include <magenta/data_pipe.h>
#include <magenta/data_pipe_producer_dispatcher.h>
#include <magenta/data_pipe_consumer_dispatcher.h>
#include <magenta/event_dispatcher.h>
#include <magenta/io_port_dispatcher.h>
#include <magenta/log_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/msg_pipe_dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/user_copy.h>
#include <magenta/user_thread.h>
#include <magenta/vm_object_dispatcher.h>
#include <magenta/wait_event.h>
#include <magenta/wait_state_observer.h>

#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <utils/string_piece.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxMessageSize = 65536u;
constexpr uint32_t kMaxMessageHandles = 1024u;

constexpr uint32_t kMaxWaitHandleCount = 256u;
constexpr mx_size_t kDefaultDataPipeCapacity = 32 * 1024u;

constexpr mx_size_t kMaxCPRNGDraw = MX_CPRNG_DRAW_MAX_LEN;
constexpr mx_size_t kMaxCPRNGSeed = MX_CPRNG_ADD_ENTROPY_MAX_LEN;

void sys_exit(int retcode) {
    LTRACEF("retcode %d\n", retcode);

    ProcessDispatcher::GetCurrent()->Exit(retcode);
}

mx_status_t sys_nanosleep(mx_time_t nanoseconds) {
    LTRACEF("nseconds %llu\n", nanoseconds);

    if (nanoseconds == 0ull) {
        thread_yield();
        return NO_ERROR;
    }

    lk_time_t t = mx_time_to_lk(nanoseconds);
    if ((nanoseconds > 0ull) && (t == 0u))
        t = 1u;

    /* sleep with interruptable flag set */
    status_t err = thread_sleep_etc(t, true);

    return err;
}

uint sys_num_cpus(void) {
    return arch_max_num_cpus();
}

uint64_t sys_current_time() {
    return current_time_hires() * 1000;  // microseconds to nanoseconds
}

mx_status_t sys_handle_wait_one(mx_handle_t handle_value,
                                mx_signals_t signals,
                                mx_time_t timeout,
                                mx_signals_state_t* _signals_state) {
    LTRACEF("handle %u\n", handle_value);

    WaitEvent event;

    status_t result;
    WaitStateObserver wait_state_observer;

    {
        auto up = ProcessDispatcher::GetCurrent();
        AutoLock lock(up->handle_table_lock());

        Handle* handle = up->GetHandle_NoLock(handle_value);
        if (!handle)
            return ERR_INVALID_ARGS;
        if (!magenta_rights_check(handle->rights(), MX_RIGHT_READ))
            return ERR_ACCESS_DENIED;

        result = wait_state_observer.Begin(&event, handle, signals, 0u);
        if (result != NO_ERROR)
            return result;
    }

    lk_time_t t = mx_time_to_lk(timeout);
    if ((timeout > 0ull) && (t == 0u))
        t = 1u;

    result = WaitEvent::ResultToStatus(event.Wait(t, nullptr));

    // Regardless of wait outcome, we must call End().
    auto signals_state = wait_state_observer.End();

    if (_signals_state) {
        if (copy_to_user(_signals_state, &signals_state, sizeof(signals_state)) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    return result;
}

mx_status_t sys_handle_wait_many(uint32_t count,
                                 const mx_handle_t* _handle_values,
                                 const mx_signals_t* _signals,
                                 mx_time_t timeout,
                                 uint32_t* _result_index,
                                 mx_signals_state_t* _signals_states) {
    LTRACEF("count %u\n", count);

    if (!_handle_values || !_signals)
        return ERR_INVALID_ARGS;
    if (!count || count > kMaxWaitHandleCount)
        return ERR_INVALID_ARGS;

    uint32_t max_size = kMaxWaitHandleCount * sizeof(uint32_t);
    uint32_t bytes_size = static_cast<uint32_t>(sizeof(uint32_t) * count);

    uint8_t* copy;
    status_t result;

    result = magenta_copy_user_dynamic(_handle_values, &copy, bytes_size, max_size);
    if (result != NO_ERROR)
        return result;
    utils::unique_ptr<int32_t[]> handle_values(reinterpret_cast<mx_handle_t*>(copy));

    result = magenta_copy_user_dynamic(_signals, &copy, bytes_size, max_size);
    if (result != NO_ERROR)
        return result;
    utils::unique_ptr<uint32_t[]> signals(reinterpret_cast<mx_signals_t*>(copy));

    utils::unique_ptr<WaitStateObserver[]> wait_state_observers(new WaitStateObserver[count]);
    if (!wait_state_observers)
        return ERR_NO_MEMORY;

    utils::unique_ptr<mx_signals_state_t[]> signals_states;
    if (_signals_states) {
        signals_states.reset(new mx_signals_state_t[count]);
        if (!signals_states)
            return ERR_NO_MEMORY;
    }

    WaitEvent event;

    // We may need to unwind (which can be done outside the lock).
    result = NO_ERROR;
    size_t num_added = 0;
    {
        auto up = ProcessDispatcher::GetCurrent();
        AutoLock lock(up->handle_table_lock());

        for (; num_added != count; ++num_added) {
            Handle* handle = up->GetHandle_NoLock(handle_values[num_added]);
            if (!handle) {
                result = ERR_INVALID_ARGS;
                break;
            }
            if (!magenta_rights_check(handle->rights(), MX_RIGHT_READ)) {
                result = ERR_ACCESS_DENIED;
                break;
            }

            result = wait_state_observers[num_added].Begin(&event, handle, signals[num_added],
                                                           static_cast<uint64_t>(num_added));
            if (result != NO_ERROR)
                break;
        }
    }
    if (result != NO_ERROR) {
        DEBUG_ASSERT(num_added < count);
        for (size_t ix = 0; ix < num_added; ++ix)
            wait_state_observers[ix].End();
        return result;
    }

    lk_time_t t = mx_time_to_lk(timeout);
    if ((timeout > 0ull) && (t == 0u))
        t = 1u;

    uint64_t context = -1;
    WaitEvent::Result wait_event_result = event.Wait(t, &context);
    result = WaitEvent::ResultToStatus(wait_event_result);

    // Regardless of wait outcome, we must call End().
    for (size_t ix = 0; ix != count; ++ix) {
        auto s = wait_state_observers[ix].End();
        if (signals_states)
            signals_states[ix] = s;
    }

    if (_result_index && WaitEvent::HaveContextForResult(wait_event_result)) {
        if (copy_to_user_u32(_result_index, static_cast<uint32_t>(context)) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    if (_signals_states) {
        if (copy_to_user(_signals_states, signals_states.get(),
                         sizeof(mx_signals_state_t) * count) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    return result;
}

mx_status_t sys_handle_close(mx_handle_t handle_value) {
    LTRACEF("handle %u\n", handle_value);
    auto up = ProcessDispatcher::GetCurrent();
    HandleUniquePtr handle(up->RemoveHandle(handle_value));
    if (!handle)
        return ERR_INVALID_ARGS;
    return NO_ERROR;
}

mx_handle_t sys_handle_duplicate(mx_handle_t handle_value, mx_rights_t rights) {
    LTRACEF("handle %u\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    HandleUniquePtr dest;

    {
        AutoLock lock(up->handle_table_lock());
        Handle* source = up->GetHandle_NoLock(handle_value);
        if (!source)
            return ERR_BAD_HANDLE;

        if (!magenta_rights_check(source->rights(), MX_RIGHT_DUPLICATE))
            return ERR_ACCESS_DENIED;
        if (rights == MX_RIGHT_SAME_RIGHTS) {
            dest.reset(DupHandle(source, source->rights()));
        } else {
            if ((source->rights() & rights) != rights)
                return ERR_INVALID_ARGS;
            dest.reset(DupHandle(source, rights));
        }
    }

    if (!dest)
        return ERR_NO_MEMORY;

    mx_handle_t dup_hv = up->MapHandleToValue(dest.get());

    up->AddHandle(utils::move(dest));
    return dup_hv;
}

mx_ssize_t sys_handle_get_info(mx_handle_t handle, uint32_t topic, void* _info, mx_size_t info_size) {
    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_BAD_HANDLE;

    switch (topic) {
        case MX_INFO_HANDLE_VALID:
            return NO_ERROR;
        case MX_INFO_HANDLE_BASIC: {
            if (!_info)
                return ERR_INVALID_ARGS;

            if (info_size < sizeof(mx_handle_basic_info_t))
                return ERR_NOT_ENOUGH_BUFFER;

            mx_handle_basic_info_t info = {
                rights,
                dispatcher->GetType(),
                dispatcher->get_state_tracker() ? MX_OBJ_PROP_WAITABLE : MX_OBJ_PROP_NONE
            };

            if (copy_to_user(reinterpret_cast<uint8_t*>(_info), &info, sizeof(info)) != NO_ERROR)
                return ERR_INVALID_ARGS;

            return sizeof(mx_handle_basic_info_t);
        }
        case MX_INFO_PROCESS: {
            if (!_info)
                return ERR_INVALID_ARGS;

            if (info_size < sizeof(mx_process_info_t))
                return ERR_NOT_ENOUGH_BUFFER;

            auto process = dispatcher->get_process_dispatcher();
            if (!process)
                return ERR_BAD_HANDLE;

            if (!magenta_rights_check(rights, MX_RIGHT_READ))
                return ERR_ACCESS_DENIED;

            mx_process_info_t info;
            auto err = process->GetInfo(&info);
            if (err != NO_ERROR)
                return err;

            if (copy_to_user(reinterpret_cast<uint8_t*>(_info), &info, sizeof(info)) != NO_ERROR)
                return ERR_INVALID_ARGS;

            return sizeof(mx_process_info_t);
        }
        default:
            return ERR_INVALID_ARGS;
    }
}

mx_status_t sys_message_read(mx_handle_t handle_value, void* _bytes, uint32_t* _num_bytes,
                             mx_handle_t* _handles, uint32_t* _num_handles, uint32_t flags) {
    LTRACEF("handle %d bytes %p num_bytes %p handles %p num_handles %p flags 0x%x\n",
            handle_value, _bytes, _num_bytes, _handles, _num_handles, flags);

    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto msg_pipe = dispatcher->get_message_pipe_dispatcher();
    if (!msg_pipe)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    uint32_t num_bytes = 0;
    uint32_t num_handles = 0;

    if (_num_bytes) {
        if (copy_from_user_u32(&num_bytes, _num_bytes) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    if (_num_handles) {
        if (copy_from_user_u32(&num_handles, _num_handles) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    if (_bytes != 0u && !_num_bytes)
        return ERR_INVALID_ARGS;
    if (_handles != 0u && !_num_handles)
        return ERR_INVALID_ARGS;

    utils::unique_ptr<uint32_t[]> handles;

    if (num_handles) {
        handles.reset(new uint32_t[num_handles]());
        if (!handles)
            return ERR_NO_MEMORY;
    }

    uint32_t next_message_size = 0u;
    uint32_t next_message_num_handles = 0u;
    status_t result = msg_pipe->BeginRead(&next_message_size, &next_message_num_handles);
    if (result != NO_ERROR)
        return result;

    // Always set the actual size and handle count so the caller can provide larger buffers.
    if (_num_bytes) {
        if (copy_to_user_u32(_num_bytes, next_message_size) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }
    if (_num_handles) {
        if (copy_to_user_u32(_num_handles, next_message_num_handles) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    // If the caller provided buffers are too small, abort the read so the caller can try again.
    if (num_bytes < next_message_size || num_handles < next_message_num_handles)
        return ERR_NOT_ENOUGH_BUFFER;

    // OK, now we can accept the message.
    utils::Array<uint8_t> bytes;
    utils::Array<Handle*> handle_list;

    result = msg_pipe->AcceptRead(&bytes, &handle_list);

    if (_bytes) {
        if (copy_to_user(reinterpret_cast<uint8_t*>(_bytes), bytes.get(), num_bytes) != NO_ERROR) {
            // $$$ free handles.
            return ERR_INVALID_ARGS;
        }
    }

    if (next_message_num_handles != 0u) {
        for (size_t ix = 0u; ix < next_message_num_handles; ++ix) {
            auto hv = up->MapHandleToValue(handle_list[ix]);
            if (copy_to_user_32(&_handles[ix], hv) != NO_ERROR) {
                // $$$ free handles.
                return ERR_INVALID_ARGS;
            }
        }
    }

    for (size_t idx = 0u; idx < next_message_num_handles; ++idx) {
        if (handle_list[idx]->dispatcher()->get_state_tracker())
            handle_list[idx]->dispatcher()->get_state_tracker()->Cancel(handle_list[idx]);
        HandleUniquePtr handle(handle_list[idx]);
        up->AddHandle(utils::move(handle));
    }

    return result;
}

mx_status_t sys_message_write(mx_handle_t handle_value, const void* _bytes, uint32_t num_bytes,
                              const mx_handle_t* _handles, uint32_t num_handles, uint32_t flags) {
    LTRACEF("handle %d bytes %p num_bytes %u handles %p num_handles %u flags 0x%x\n",
            handle_value, _bytes, num_bytes, _handles, num_handles, flags);

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto msg_pipe = dispatcher->get_message_pipe_dispatcher();
    if (!msg_pipe)
        return ERR_BAD_HANDLE;

    bool is_reply_pipe = msg_pipe->is_reply_pipe();

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    if (num_bytes != 0u && !_bytes)
        return ERR_INVALID_ARGS;
    if (num_handles != 0u && !_handles)
        return ERR_INVALID_ARGS;

    if (num_bytes > kMaxMessageSize)
        return ERR_TOO_BIG;
    if (num_handles > kMaxMessageHandles)
        return ERR_TOO_BIG;

    status_t result;
    utils::Array<uint8_t> bytes;

    if (num_bytes) {
        uint8_t* copy;
        result = magenta_copy_user_dynamic(_bytes, &copy, num_bytes, kMaxMessageSize);
        if (result != NO_ERROR)
            return result;
        bytes.reset(copy, num_bytes);
    }

    utils::unique_ptr<mx_handle_t[], utils::free_delete> handles;
    if (num_handles) {
        void* c_handles;
        status_t status = copy_from_user_dynamic(
            &c_handles, _handles, num_handles * sizeof(_handles[0]), kMaxMessageHandles);
        // |status| can be ERR_NO_MEMORY or ERR_INVALID_ARGS.
        if (status != NO_ERROR)
            return status;

        handles.reset(static_cast<mx_handle_t*>(c_handles));
    }

    utils::Array<Handle*> handle_list(new Handle*[num_handles], num_handles);
    {
        // Loop twice, first we collect and validate handles, the second pass
        // we remove them from this process.
        AutoLock lock(up->handle_table_lock());

        size_t reply_pipe_found = -1;

        for (size_t ix = 0; ix != num_handles; ++ix) {
            auto handle = up->GetHandle_NoLock(handles[ix]);
            if (!handle)
                return ERR_INVALID_ARGS;

            if (handle->dispatcher() == dispatcher) {
                // Found itself, which is only allowed for MX_FLAG_REPLY_PIPE (aka Reply) pipes.
                if (!is_reply_pipe) {
                    return ERR_NOT_SUPPORTED;
                } else {
                    reply_pipe_found = ix;
                }
            }

            if (!magenta_rights_check(handle->rights(), MX_RIGHT_TRANSFER))
                return ERR_ACCESS_DENIED;

            handle_list[ix] = handle;
        }

        if (is_reply_pipe) {
            // For reply pipes, itself must be in the handle array and be the last handle.
            if ((num_handles == 0) || (reply_pipe_found != (num_handles - 1)))
                return ERR_BAD_STATE;
        }

        for (size_t ix = 0; ix != num_handles; ++ix) {
            up->RemoveHandle_NoLock(handles[ix]).release();
        }
    }

    result = msg_pipe->Write(utils::move(bytes), utils::move(handle_list));

    if (result != NO_ERROR) {
        // Write failed, put back the handles into this process.
        AutoLock lock(up->handle_table_lock());
        for (size_t ix = 0; ix != num_handles; ++ix) {
            up->UndoRemoveHandle_NoLock(handles[ix]);
        }
    }

    return result;
}

mx_status_t sys_message_pipe_create(mx_handle_t out_handle[2], uint32_t flags) {
    LTRACEF("entry out_handle[] %p\n", out_handle);

    if (!out_handle)
        return ERR_INVALID_ARGS;

    if ((flags != 0u) && (flags != MX_FLAG_REPLY_PIPE))
        return ERR_INVALID_ARGS;

    utils::RefPtr<Dispatcher> mpd0, mpd1;
    mx_rights_t rights;
    status_t result = MessagePipeDispatcher::Create(flags, &mpd0, &mpd1, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr h0(MakeHandle(utils::move(mpd0), rights));
    if (!h0)
        return ERR_NO_MEMORY;

    HandleUniquePtr h1(MakeHandle(utils::move(mpd1), rights));
    if (!h1)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t hv[2] = {up->MapHandleToValue(h0.get()), up->MapHandleToValue(h1.get())};

    if (copy_to_user(out_handle, hv, sizeof(mx_handle_t) * 2) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(utils::move(h0));
    up->AddHandle(utils::move(h1));

    LTRACE_EXIT;
    return NO_ERROR;
}

mx_handle_t sys_thread_create(int (*entry)(void*), void* arg, const char* name, uint32_t name_len) {
    LTRACEF("entry %p\n", entry);

    char buf[MX_MAX_NAME_LEN];
    utils::StringPiece sp;
    status_t result = magenta_copy_user_string(name, name_len, buf, sizeof(buf), &sp);
    if (result != NO_ERROR)
        return result;

    auto up = ProcessDispatcher::GetCurrent();

    auto user_thread = utils::AdoptRef(
        new UserThread(utils::RefPtr<ProcessDispatcher>(up), entry, arg));
    if (!user_thread)
        return ERR_NO_MEMORY;

    result = user_thread->Initialize(buf);
    if (result != NO_ERROR)
        return result;

    user_thread->Start();

    utils::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    result = ThreadDispatcher::Create(utils::move(user_thread), &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(utils::move(dispatcher), rights));

    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(utils::move(handle));
    return hv;
}

void sys_thread_exit() {
    LTRACE_ENTRY;
    UserThread::GetCurrent()->Exit();
}

extern "C" {
uint64_t get_tsc_ticks_per_ms(void);
};

mx_status_t sys_thread_arch_prctl(mx_handle_t handle_value, uint32_t op, uintptr_t* value_ptr) {
    LTRACEF("handle %u operation %u value_ptr %p", handle_value, op, value_ptr);

    // TODO(cpu) what to do with |handle_value|?

    uintptr_t value;

    switch (op) {
#ifdef ARCH_X86_64
    case ARCH_SET_FS:
        if (copy_from_user_uptr(&value, value_ptr) != NO_ERROR)
            return ERR_INVALID_ARGS;
        if (!x86_is_vaddr_canonical(value))
            return ERR_INVALID_ARGS;
        write_msr(X86_MSR_IA32_FS_BASE, value);
        break;
    case ARCH_GET_FS:
        value = read_msr(X86_MSR_IA32_FS_BASE);
        if (copy_to_user_uptr(value_ptr, value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        break;
    case ARCH_SET_GS:
        if (copy_from_user_uptr(&value, value_ptr) != NO_ERROR)
            return ERR_INVALID_ARGS;
        if (!x86_is_vaddr_canonical(value))
            return ERR_INVALID_ARGS;
        write_msr(X86_MSR_IA32_KERNEL_GS_BASE, value);
        break;
    case ARCH_GET_GS:
        value = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);
        if (copy_to_user_uptr(value_ptr, value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        break;
    case ARCH_GET_TSC_TICKS_PER_MS:
        value = get_tsc_ticks_per_ms();
        if (copy_to_user_uptr(value_ptr, value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        break;
#elif ARCH_ARM64
    case ARCH_SET_TPIDRRO_EL0:
        if (copy_from_user_uptr(&value, value_ptr) != NO_ERROR)
            return ERR_INVALID_ARGS;
        ARM64_WRITE_SYSREG(tpidrro_el0, value);
        break;
#elif ARCH_ARM
    case ARCH_SET_CP15_READONLY:
        if (copy_from_user_uptr(&value, value_ptr) != NO_ERROR)
            return ERR_INVALID_ARGS;
        __asm__ volatile("mcr p15, 0, %0, c13, c0, 3" : : "r" (value));
        ISB;
        break;
#endif
    default:
        return ERR_INVALID_ARGS;
    }

    return NO_ERROR;
}

mx_handle_t sys_process_create(const char* name, uint32_t name_len) {
    LTRACEF("name %s\n", name);

    char buf[MX_MAX_NAME_LEN];
    utils::StringPiece sp;
    status_t result = magenta_copy_user_string(name, name_len, buf, sizeof(buf), &sp);
    if (result != NO_ERROR)
        return result;

    utils::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    status_t res = ProcessDispatcher::Create(sp, &dispatcher, &rights);
    if (res != NO_ERROR)
        return res;

    HandleUniquePtr handle(MakeHandle(utils::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(utils::move(handle));
    return hv;
}

mx_status_t sys_process_start(mx_handle_t handle_value, mx_handle_t arg_handle_value, mx_vaddr_t entry) {
    LTRACEF("handle %u\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto process = dispatcher->get_process_dispatcher();
    if (!process)
        return ERR_BAD_HANDLE;

    HandleUniquePtr arg_handle = up->RemoveHandle(arg_handle_value);
    if (!arg_handle_value)
        return ERR_INVALID_ARGS;

    auto arg_nhv = process->MapHandleToValue(arg_handle.get());
    process->AddHandle(utils::move(arg_handle));

    // TODO(cpu) if Start() fails we want to undo RemoveHandle().

    return process->Start(reinterpret_cast<void*>(arg_nhv), entry);
}

mx_handle_t sys_event_create(uint32_t options) {
    LTRACEF("options 0x%x\n", options);

    utils::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    status_t result = EventDispatcher::Create(options, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(utils::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(utils::move(handle));
    return hv;
}

mx_status_t sys_event_signal(mx_handle_t handle_value) {
    LTRACEF("handle %u\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto event = dispatcher->get_event_dispatcher();
    if (!event)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    return event->SignalEvent();
}

mx_status_t sys_event_reset(mx_handle_t handle_value) {
    LTRACEF("handle %u\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto event = dispatcher->get_event_dispatcher();
    if (!event)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    return event->ResetEvent();
}

mx_status_t sys_object_signal(mx_handle_t handle_value, uint32_t set_mask, uint32_t clear_mask) {
    LTRACEF("handle %u\n", handle_value);

    if ((set_mask & MX_SIGNAL_USER_ALL) != set_mask)
        return ERR_INVALID_ARGS;
    if ((clear_mask & MX_SIGNAL_USER_ALL) != clear_mask)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return ERR_INVALID_ARGS;
    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    return dispatcher->UserSignal(set_mask, clear_mask);
}

mx_status_t sys_futex_wait(int* value_ptr, int current_value, mx_time_t timeout) {
    return ProcessDispatcher::GetCurrent()->futex_context()->FutexWait(value_ptr, current_value, timeout);
}

mx_status_t sys_futex_wake(int* value_ptr, uint32_t count) {
    return ProcessDispatcher::GetCurrent()->futex_context()->FutexWake(value_ptr, count);
}

mx_status_t sys_futex_requeue(int* wake_ptr, uint32_t wake_count, int current_value,
                              int* requeue_ptr, uint32_t requeue_count) {
    return ProcessDispatcher::GetCurrent()->futex_context()->FutexRequeue(
        wake_ptr, wake_count, current_value, requeue_ptr, requeue_count);
}

mx_handle_t sys_vm_object_create(uint64_t size) {
    LTRACEF("size 0x%llx\n", size);

    // create a vm object
    utils::RefPtr<VmObject> vmo = VmObject::Create(0, size);
    if (!vmo)
        return ERR_NO_MEMORY;

    // create a Vm Object dispatcher
    utils::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = VmObjectDispatcher::Create(utils::move(vmo), &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    // create a handle and attach the dispatcher to it
    HandleUniquePtr handle(MakeHandle(utils::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(utils::move(handle));

    return hv;
}

mx_ssize_t sys_vm_object_read(mx_handle_t handle, void* data, uint64_t offset, mx_size_t len) {
    LTRACEF("handle %d, data %p, offset 0x%llx, len 0x%lx\n", handle, data, offset, len);

    // lookup the dispatcher from handle
    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto vmo = dispatcher->get_vm_object_dispatcher();
    if (!vmo)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    // do the read operation
    return vmo->Read(data, len, offset);
}

mx_ssize_t sys_vm_object_write(mx_handle_t handle, const void* data, uint64_t offset, mx_size_t len) {
    LTRACEF("handle %d, data %p, offset 0x%llx, len 0x%lx\n", handle, data, offset, len);

    // lookup the dispatcher from handle
    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto vmo = dispatcher->get_vm_object_dispatcher();
    if (!vmo)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    // do the write operation
    return vmo->Write(data, len, offset);
}

mx_status_t sys_vm_object_get_size(mx_handle_t handle, uint64_t* _size) {
    LTRACEF("handle %d, sizep %p\n", handle, _size);

    // lookup the dispatcher from handle
    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto vmo = dispatcher->get_vm_object_dispatcher();
    if (!vmo)
        return ERR_BAD_HANDLE;

    // no rights check, anyone should be able to get the size

    // do the operation
    uint64_t size = 0;
    mx_status_t status = vmo->GetSize(&size);

    // copy the size back, even if it failed
    if (copy_to_user(reinterpret_cast<uint8_t*>(_size), &size, sizeof(size)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return status;
}

mx_status_t sys_vm_object_set_size(mx_handle_t handle, uint64_t size) {
    LTRACEF("handle %d, size 0x%llx\n", handle, size);

    // lookup the dispatcher from handle
    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto vmo = dispatcher->get_vm_object_dispatcher();
    if (!vmo)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    // do the operation
    return vmo->SetSize(size);
}

mx_status_t sys_process_vm_map(mx_handle_t proc_handle, mx_handle_t vmo_handle,
                               uint64_t offset, mx_size_t len, uintptr_t* user_ptr, uint32_t flags) {

    LTRACEF("proc handle %d, vmo handle %d, offset 0x%llx, len 0x%lx, user_ptr %p, flags 0x%x\n",
            proc_handle, vmo_handle, offset, len, user_ptr, flags);

    // current process
    auto up = ProcessDispatcher::GetCurrent();

    // get the vmo dispatcher
    utils::RefPtr<Dispatcher> vmo_dispatcher;
    uint32_t vmo_rights;
    if (!up->GetDispatcher(vmo_handle, &vmo_dispatcher, &vmo_rights))
        return ERR_INVALID_ARGS;

    auto vmo = vmo_dispatcher->get_vm_object_dispatcher();
    if (!vmo)
        return ERR_BAD_HANDLE;

    // get a reffed pointer to the address space in the target process
    utils::RefPtr<VmAspace> aspace;
    if (proc_handle == 0) {
        // handle 0 is magic for 'current process'
        // TODO: remove this hack and switch to requiring user to pass the current process handle
        aspace = up->aspace();
    } else {
        // get the process dispatcher and convert to aspace
        utils::RefPtr<Dispatcher> proc_dispatcher;
        uint32_t proc_rights;
        if (!up->GetDispatcher(proc_handle, &proc_dispatcher, &proc_rights))
            return ERR_INVALID_ARGS;

        auto process = proc_dispatcher->get_process_dispatcher();
        if (!process)
            return ERR_BAD_HANDLE;

        if (!magenta_rights_check(proc_rights, MX_RIGHT_WRITE))
            return ERR_ACCESS_DENIED;

        // get the address space out of the process dispatcher
        aspace = process->aspace();
        if (!aspace)
            return ERR_INVALID_ARGS;
    }

    // copy the user pointer in
    uintptr_t ptr;
    if (copy_from_user(&ptr, reinterpret_cast<uint8_t*>(user_ptr), sizeof(ptr)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    // do the map call
    mx_status_t status = vmo->Map(utils::move(aspace), vmo_rights, offset, len, &ptr, flags);
    if (status != NO_ERROR)
        return status;

    // copy the user pointer back
    if (copy_to_user(reinterpret_cast<uint8_t*>(user_ptr), &ptr, sizeof(ptr)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

mx_status_t sys_process_vm_unmap(mx_handle_t proc_handle, uintptr_t address, mx_size_t len) {
    LTRACEF("proc handle %d, address 0x%lx, len 0x%lx\n", proc_handle, address, len);

    // get a reffed pointer to the address space in the target process
    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<VmAspace> aspace;
    if (proc_handle == 0) {
        // handle 0 is magic for 'current process'
        // TODO: remove this hack and switch to requiring user to pass the current process handle
        aspace = up->aspace();
    } else {
        // get the process dispatcher and convert to aspace
        utils::RefPtr<Dispatcher> proc_dispatcher;
        uint32_t proc_rights;
        if (!up->GetDispatcher(proc_handle, &proc_dispatcher, &proc_rights))
            return ERR_INVALID_ARGS;

        auto process = proc_dispatcher->get_process_dispatcher();
        if (!process)
            return ERR_BAD_HANDLE;

        if (!magenta_rights_check(proc_rights, MX_RIGHT_WRITE))
            return ERR_ACCESS_DENIED;

        aspace = process->aspace();
        if (!aspace)
            return ERR_INVALID_ARGS;
    }

    // TODO: support range unmapping
    // at the moment only support unmapping what is at a given address, signalled with len = 0
    if (len != 0)
        return ERR_INVALID_ARGS;

    // TODO: get the unmap call into the dispatcher
    // It's not really feasible to do it because of the handle 0 hack at the moment, and there's
    // not a good way to get to the current dispatcher without going through a handle
    return aspace->FreeRegion(address);
}

mx_status_t sys_process_vm_protect(mx_handle_t proc_handle, uintptr_t address, mx_size_t len,
                                   uint32_t prot) {
    LTRACEF("proc handle %d, address 0x%lx, len 0x%lx, prot 0x%x\n", proc_handle, address, len, prot);

    // get a reffed pointer to the address space in the target process
    auto up = ProcessDispatcher::GetCurrent();
    utils::RefPtr<VmAspace> aspace;
    if (proc_handle == 0) {
        // handle 0 is magic for 'current process'
        // TODO: remove this hack and switch to requiring user to pass the current process handle
        aspace = up->aspace();
    } else {
        // get the process dispatcher and convert to aspace
        utils::RefPtr<Dispatcher> proc_dispatcher;
        uint32_t proc_rights;
        if (!up->GetDispatcher(proc_handle, &proc_dispatcher, &proc_rights))
            return ERR_INVALID_ARGS;

        auto process = proc_dispatcher->get_process_dispatcher();
        if (!process)
            return ERR_BAD_HANDLE;

        if (!magenta_rights_check(proc_rights, MX_RIGHT_WRITE))
            return ERR_ACCESS_DENIED;

        aspace = process->aspace();
        if (!aspace)
            return ERR_INVALID_ARGS;
    }

    // TODO: support range protect
    // at the moment only support protecting what is at a given address, signalled with len = 0
    if (len != 0)
        return ERR_INVALID_ARGS;

    auto r = aspace->FindRegion(address);
    if (!r)
        return ERR_INVALID_ARGS;

    uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_USER;
    switch (prot & (MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE)) {
    case MX_VM_FLAG_PERM_READ:
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_RO;
        break;
    case MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE:
        // default flags
        break;
    case 0: // no way to express no permissions
    case MX_VM_FLAG_PERM_WRITE:
        // no way to express write only
        return ERR_INVALID_ARGS;
    }

    if ((prot & MX_VM_FLAG_PERM_EXECUTE) == 0) {
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_NO_EXECUTE;
    }

    return r->Protect(arch_mmu_flags);
}

int sys_log_create(uint32_t flags) {
    LTRACEF("flags 0x%x\n", flags);

    // kernel flag is forbidden to userspace
    flags &= (~DLOG_FLAG_KERNEL);

    // create a Log dispatcher
    utils::RefPtr<Dispatcher> dispatcher;
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
    HandleUniquePtr handle(MakeHandle(utils::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(utils::move(handle));

    return hv;
}

int sys_log_write(mx_handle_t log_handle, uint32_t len, const void* ptr, uint32_t flags) {
    LTRACEF("log handle %d, len 0x%x, ptr 0x%p\n", log_handle, len, ptr);

    if (len > DLOG_MAX_ENTRY)
        return ERR_TOO_BIG;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> log_dispatcher;
    uint32_t log_rights;
    if (!up->GetDispatcher(log_handle, &log_dispatcher, &log_rights))
        return ERR_INVALID_ARGS;

    auto log = log_dispatcher->get_log_dispatcher();
    if (!log)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(log_rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    char buf[DLOG_MAX_ENTRY];
    if (magenta_copy_from_user(ptr, buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return log->Write(buf, len, flags);
}

int sys_log_read(mx_handle_t log_handle, uint32_t len, void* ptr, uint32_t flags) {
    LTRACEF("log handle %d, len 0x%x, ptr 0x%p\n", log_handle, len, ptr);

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> log_dispatcher;
    uint32_t log_rights;
    if (!up->GetDispatcher(log_handle, &log_dispatcher, &log_rights))
        return ERR_INVALID_ARGS;

    auto log = log_dispatcher->get_log_dispatcher();
    if (!log)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(log_rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    return log->ReadFromUser(ptr, len, flags);
}

mx_handle_t sys_io_port_create(uint32_t options) {
    LTRACEF("options %u\n", options);

    utils::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = IOPortDispatcher::Create(options, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(utils::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(utils::move(handle));

    return hv;
}

mx_status_t sys_io_port_queue(mx_handle_t handle, const void* packet, mx_size_t size) {
    LTRACEF("handle %d\n", handle);

    IOP_Packet iop;

    if (size < sizeof(iop.u))
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto ioport = dispatcher->get_io_port_dispatcher();
    if (!ioport)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    if (magenta_copy_from_user(packet, &iop.u, sizeof(iop.u)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return ioport->Queue(&iop);
}

mx_status_t sys_io_port_wait(mx_handle_t handle, void* packet, mx_size_t size) {
    LTRACEF("handle %d\n", handle);

    IOP_Packet iop;

    if (size < sizeof(iop.u))
        return ERR_INVALID_ARGS;

    if (!packet)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto ioport = dispatcher->get_io_port_dispatcher();
    if (!ioport)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    mx_status_t status = ioport->Wait(&iop);
    if (status < 0)
        return status;

    if (copy_to_user(packet, &iop.u, sizeof(iop.u)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

mx_status_t sys_io_port_bind(mx_handle_t handle, uint64_t key, mx_handle_t source, mx_signals_t signals) {
    LTRACEF("handle %d source %d\n", handle, source);

    auto up = ProcessDispatcher::GetCurrent();
    uint32_t rights = 0;

    utils::RefPtr<Dispatcher> iop_d;
    if (!up->GetDispatcher(handle, &iop_d, &rights))
        return ERR_INVALID_ARGS;

    auto ioport = iop_d->get_io_port_dispatcher();
    if (!ioport)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    utils::RefPtr<Dispatcher> src_d;
    if (!up->GetDispatcher(source, &src_d, &rights))
        return ERR_INVALID_ARGS;

    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    auto state_tracker = src_d->get_state_tracker();
    if (!state_tracker)
        return ERR_INVALID_ARGS;

    return state_tracker->BindIOPort(utils::RefPtr<IOPortDispatcher>(ioport), key, signals) ?
        NO_ERROR : ERR_NOT_READY;
}

mx_handle_t sys_data_pipe_create(uint32_t options, mx_size_t element_size, mx_size_t capacity,
                                 mx_handle_t* _handle) {
    LTRACEF("options %u\n", options);

    if (!_handle)
        return ERR_INVALID_ARGS;

    // TODO(cpu): support different element sizes.
    if (element_size != 1u)
        return ERR_INVALID_ARGS;


    utils::RefPtr<Dispatcher> producer_dispatcher;
    mx_rights_t producer_rights;

    utils::RefPtr<Dispatcher> consumer_dispatcher;
    mx_rights_t consumer_rights;

    mx_status_t result = DataPipe::Create(capacity ? capacity : kDefaultDataPipeCapacity,
                                          &producer_dispatcher,
                                          &consumer_dispatcher,
                                          &producer_rights,
                                          &consumer_rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr producer_handle(MakeHandle(utils::move(producer_dispatcher), producer_rights));
    if (!producer_handle)
        return ERR_NO_MEMORY;

    HandleUniquePtr consumer_handle(MakeHandle(utils::move(consumer_dispatcher), consumer_rights));
    if (!consumer_handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t hv_producer = up->MapHandleToValue(producer_handle.get());
    mx_handle_t hv_consumer = up->MapHandleToValue(consumer_handle.get());

    if (copy_to_user_32(_handle, hv_consumer) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(utils::move(producer_handle));
    up->AddHandle(utils::move(consumer_handle));

    return hv_producer;
}

mx_ssize_t sys_data_pipe_write(mx_handle_t handle, uint32_t flags, mx_size_t requested,
                               void const* _buffer) {
    LTRACEF("handle %d\n", handle);

    if (!_buffer)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto producer = dispatcher->get_data_pipe_producer_dispatcher();
    if (!producer)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    mx_size_t written = requested;
    mx_status_t st = producer->Write(_buffer, &written);
    if (st < 0)
        return st;

    return written;
}

mx_ssize_t sys_data_pipe_read(mx_handle_t handle, uint32_t flags, mx_size_t requested,
                              void* _buffer) {
    LTRACEF("handle %d\n", handle);

    if (!_buffer)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto consumer = dispatcher->get_data_pipe_consumer_dispatcher();
    if (!consumer)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    mx_size_t read = requested;
    mx_status_t st = consumer->Read(_buffer, &read);
    if (st < 0)
        return st;

    return read;
}

mx_ssize_t sys_data_pipe_begin_write(mx_handle_t handle, uint32_t flags, mx_size_t requested,
                                     uintptr_t* buffer) {
    LTRACEF("handle %d\n", handle);

    if (!buffer)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto producer = dispatcher->get_data_pipe_producer_dispatcher();
    if (!producer)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    mx_status_t status;
    uintptr_t user_addr = 0u;

    status = producer->BeginWrite(up->aspace(), reinterpret_cast<void**>(&user_addr), &requested);
    if (status != NO_ERROR)
        return status;

    if (copy_to_user_uptr(buffer, user_addr) != NO_ERROR) {
        producer->EndWrite(0u);
        return ERR_INVALID_ARGS;
    }

    return requested;
}

mx_status_t sys_data_pipe_end_write(mx_handle_t handle, mx_size_t written) {
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto producer = dispatcher->get_data_pipe_producer_dispatcher();
    if (!producer)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    return producer->EndWrite(written);
}

mx_ssize_t sys_data_pipe_begin_read(mx_handle_t handle, uint32_t flags, mx_size_t requested,
                                    uintptr_t* buffer) {
    LTRACEF("handle %d\n", handle);

    if (!buffer)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto consumer = dispatcher->get_data_pipe_consumer_dispatcher();
    if (!consumer)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    mx_status_t status;
    uintptr_t user_addr = 0u;

    status = consumer->BeginRead(up->aspace(), reinterpret_cast<void**>(&user_addr), &requested);
    if (status != NO_ERROR)
        return status;

    if (copy_to_user_uptr(buffer, user_addr) != NO_ERROR) {
        consumer->EndRead(0u);
        return ERR_INVALID_ARGS;
    }

    return requested;
}

mx_status_t sys_data_pipe_end_read(mx_handle_t handle, mx_size_t read) {
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto consumer = dispatcher->get_data_pipe_consumer_dispatcher();
    if (!consumer)
        return ERR_BAD_HANDLE;

    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    return consumer->EndRead(read);
}

mx_ssize_t sys_cprng_draw(void* buffer, mx_size_t len) {
    if (len > kMaxCPRNGDraw)
        return ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGDraw];

    auto prng = crypto::GlobalPRNG::GetInstance();
    prng->Draw(kernel_buf, static_cast<int>(len));

    if (copy_to_user(buffer, kernel_buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    // Get rid of the stack copy of the random data
    memset(kernel_buf, 0, sizeof(kernel_buf));

    return len;
}

mx_status_t sys_cprng_add_entropy(void* buffer, mx_size_t len) {
    if (len > kMaxCPRNGSeed)
        return ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGSeed];
    if (copy_from_user(kernel_buf, buffer, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    auto prng = crypto::GlobalPRNG::GetInstance();
    prng->AddEntropy(kernel_buf, static_cast<int>(len));

    // Get rid of the stack copy of the random data
    memset(kernel_buf, 0, sizeof(kernel_buf));

    return NO_ERROR;
}
