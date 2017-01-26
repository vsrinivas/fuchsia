// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/auto_lock.h>

#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/resource_dispatcher.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/vm_address_region_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

// actual is an optional return parameter for the number of records returned
// avail is an optional return parameter for the number of records available

// Topics which return a fixed number of records will return ERR_BUFFER_TOO_SMALL
// if there is not enough buffer space provided.
// This allows for mx_object_get_info(handle, topic, &info, sizeof(info), NULL, NULL)

mx_status_t sys_object_get_info(mx_handle_t handle, uint32_t topic,
                                void* _buffer, size_t buffer_size,
                                size_t* _actual, size_t* _avail) {
    LTRACEF("handle %d topic %u\n", handle, topic);

    user_ptr<void> buffer(_buffer);
    auto up = ProcessDispatcher::GetCurrent();

    switch (topic) {
        case MX_INFO_HANDLE_VALID: {
            mxtl::RefPtr<Dispatcher> dispatcher;
            uint32_t rights;

            // test that the handle is valid at all, return error if it's not
            if (!up->GetDispatcher(handle, &dispatcher, &rights))
                return ERR_BAD_HANDLE;
            return NO_ERROR;
        }
        case MX_INFO_HANDLE_BASIC: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.
            size_t actual = (buffer_size < sizeof(mx_info_handle_basic_t)) ? 0 : 1;
            size_t avail = 1;

            mxtl::RefPtr<Dispatcher> dispatcher;
            uint32_t rights;

            if (!up->GetDispatcher(handle, &dispatcher, &rights))
                return up->BadHandle(handle, ERR_BAD_HANDLE);

            if (actual > 0) {
                bool waitable = dispatcher->get_state_tracker() != nullptr;

                // build the info structure
                mx_info_handle_basic_t info = {
                    .koid = dispatcher->get_koid(),
                    .rights = rights,
                    .type = dispatcher->get_type(),
                    .props = waitable ? MX_OBJ_PROP_WAITABLE : MX_OBJ_PROP_NONE,
                };

                if (buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;
            }
            if (_actual && (make_user_ptr(_actual).copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (make_user_ptr(_avail).copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (actual == 0)
                return ERR_BUFFER_TOO_SMALL;
            return NO_ERROR;
        }
        case MX_INFO_PROCESS: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.
            size_t actual = (buffer_size < sizeof(mx_info_process_t)) ? 0 : 1;
            size_t avail = 1;

            // grab a reference to the dispatcher
            mxtl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcher<ProcessDispatcher>(handle, &process, MX_RIGHT_READ);
            if (error < 0)
                return error;

            if (actual > 0) {
                // build the info structure
                mx_info_process_t info = { };

                auto err = process->GetInfo(&info);
                if (err != NO_ERROR)
                    return err;

                if (buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;
            }
            if (_actual && (make_user_ptr(_actual).copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (make_user_ptr(_avail).copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (actual == 0)
                return ERR_BUFFER_TOO_SMALL;
            return NO_ERROR;
        }
        case MX_INFO_PROCESS_THREADS: {
            // grab a reference to the dispatcher
            mxtl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcher<ProcessDispatcher>(handle, &process, MX_RIGHT_ENUMERATE);
            if (error < 0)
                return error;

            // Getting the list of threads is inherently racy (unless the
            // caller has already stopped all threads, but that's not our
            // concern). Still, we promise to either return all threads we know
            // about at a particular point in time, or notify the caller that
            // more threads exist than what we computed at that same point in
            // time.

            mxtl::Array<mx_koid_t> threads;
            mx_status_t status = process->GetThreads(&threads);
            if (status != NO_ERROR)
                return status;
            size_t num_threads = threads.size();
            size_t num_space_for = buffer_size / sizeof(mx_koid_t);
            size_t num_to_copy = MIN(num_threads, num_space_for);

            if (buffer.copy_array_to_user(threads.get(), sizeof(mx_koid_t) * num_to_copy) != NO_ERROR)
                return ERR_INVALID_ARGS;
            if (_actual && (make_user_ptr(_actual).copy_to_user(num_to_copy) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (make_user_ptr(_avail).copy_to_user(num_threads) != NO_ERROR))
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_INFO_RESOURCE_CHILDREN:
        case MX_INFO_RESOURCE_RECORDS: {
            mxtl::RefPtr<ResourceDispatcher> resource;
            mx_status_t status = up->GetDispatcher<ResourceDispatcher>(handle, &resource, MX_RIGHT_ENUMERATE);
            if (status < 0)
                return status;

            auto records = buffer.reinterpret<mx_rrec_t>();
            size_t count = buffer_size / sizeof(mx_rrec_t);
            size_t avail = 0;
            if (topic == MX_INFO_RESOURCE_CHILDREN) {
                status = resource->GetChildren(records, count, &count, &avail);
            } else {
                status = resource->GetRecords(records, count, &count, &avail);
            }

            if (_actual && (make_user_ptr(_actual).copy_to_user(count) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (make_user_ptr(_avail).copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            return status;
        }
        case MX_INFO_VMAR: {
            mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
            mx_rights_t rights;
            mx_status_t status = up->GetDispatcher<VmAddressRegionDispatcher>(handle,
                                                                              &vmar, &rights);
            if (status < 0)
                return status;

            size_t actual = (buffer_size < sizeof(mx_info_vmar_t)) ? 0 : 1;
            size_t avail = 1;

            if (actual > 0) {
                auto real_vmar = vmar->vmar();
                mx_info_vmar_t info = {
                    .base = real_vmar->base(),
                    .len = real_vmar->size(),
                };
                if (buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;

            }

            if (_actual && (make_user_ptr(_actual).copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (make_user_ptr(_avail).copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (actual == 0)
                return ERR_BUFFER_TOO_SMALL;
            return NO_ERROR;
        }
        default:
            return ERR_NOT_SUPPORTED;
    }
}

mx_status_t sys_object_get_property(mx_handle_t handle_value, uint32_t property,
                                    void* _value, size_t size) {
    if (!_value)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);

    if (!magenta_rights_check(rights, MX_RIGHT_GET_PROPERTY))
        return ERR_ACCESS_DENIED;

    switch (property) {
        case MX_PROP_BAD_HANDLE_POLICY: {
            if (size < sizeof(uint32_t))
                return ERR_BUFFER_TOO_SMALL;
            auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
            if (!process)
                return ERR_WRONG_TYPE;
            uint32_t value = process->get_bad_handle_policy();
            if (make_user_ptr(_value).reinterpret<uint32_t>().copy_to_user(value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_PROP_NUM_STATE_KINDS: {
            if (size != sizeof(uint32_t))
                return ERR_BUFFER_TOO_SMALL;
            auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
            if (!thread)
                return ERR_WRONG_TYPE;
            uint32_t value = thread->thread()->get_num_state_kinds();
            if (make_user_ptr(_value).reinterpret<uint32_t>().copy_to_user(value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_PROP_NAME: {
            if (size < MX_MAX_NAME_LEN)
                return ERR_BUFFER_TOO_SMALL;
            char name[MX_MAX_NAME_LEN];
            dispatcher->get_name(name);
            if (make_user_ptr(_value).copy_array_to_user(name, MX_MAX_NAME_LEN) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        default:
            return ERR_INVALID_ARGS;
    }

    __UNREACHABLE;
}

static mx_status_t is_current_thread(mxtl::RefPtr<Dispatcher>* dispatcher) {
    auto thread_dispatcher = DownCastDispatcher<ThreadDispatcher>(dispatcher);
    if (!thread_dispatcher)
        return ERR_WRONG_TYPE;
    if (thread_dispatcher->thread() != UserThread::GetCurrent())
        return ERR_ACCESS_DENIED;
    return NO_ERROR;
}

mx_status_t sys_object_set_property(mx_handle_t handle_value, uint32_t property,
                                    const void* _value, size_t size) {
    if (!_value)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);

    if (!magenta_rights_check(rights, MX_RIGHT_SET_PROPERTY))
        return up->BadHandle(handle_value, ERR_ACCESS_DENIED);

    switch (property) {
        case MX_PROP_BAD_HANDLE_POLICY: {
            if (size < sizeof(uint32_t))
                return ERR_BUFFER_TOO_SMALL;
            auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
            if (!process)
                return up->BadHandle(handle_value, ERR_WRONG_TYPE);
            uint32_t value = 0;
            if (make_user_ptr(_value).reinterpret<const uint32_t>().copy_from_user(&value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return process->set_bad_handle_policy(value);
        }
        case MX_PROP_NAME: {
            if (size >= MX_MAX_NAME_LEN)
                size = MX_MAX_NAME_LEN - 1;
            char name[MX_MAX_NAME_LEN - 1];
            if (make_user_ptr(_value).copy_array_from_user(name, size) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return dispatcher->set_name(name, size);
        }
#if ARCH_X86_64
        case MX_PROP_REGISTER_FS: {
            if (size < sizeof(uintptr_t))
                return ERR_BUFFER_TOO_SMALL;
            mx_status_t status = is_current_thread(&dispatcher);
            if (status != NO_ERROR)
                return status;
            uintptr_t addr;
            if (make_user_ptr(_value).reinterpret<const uintptr_t>().copy_from_user(&addr) != NO_ERROR)
                return ERR_INVALID_ARGS;
            if (!x86_is_vaddr_canonical(addr))
                return ERR_INVALID_ARGS;
            write_msr(X86_MSR_IA32_FS_BASE, addr);
            return NO_ERROR;
        }
#endif
    }

    return ERR_INVALID_ARGS;
}

mx_status_t sys_object_signal(mx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);
    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return up->BadHandle(handle_value, ERR_ACCESS_DENIED);

    return dispatcher->user_signal(clear_mask, set_mask, false);
}

mx_status_t sys_object_signal_peer(mx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);
    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return up->BadHandle(handle_value, ERR_ACCESS_DENIED);

    return dispatcher->user_signal(clear_mask, set_mask, true);
}

// Given a kernel object with children objects, obtain a handle to the
// child specified by the provided kernel object id.
//
// MX_HANDLE_INVALID is currently treated as a "magic" handle used to
// object a process from "the system".
mx_status_t sys_object_get_child(mx_handle_t handle, uint64_t koid, mx_rights_t rights,
                                 mx_handle_t* _out) {
    auto up = ProcessDispatcher::GetCurrent();

    if (handle == MX_HANDLE_INVALID) {
        //TODO: lookup process from job instead of treating INVALID as magic
        const auto kDebugRights =
            MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
            MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY | MX_RIGHT_ENUMERATE;

        if (rights == MX_RIGHT_SAME_RIGHTS) {
            rights = kDebugRights;
        } else if ((kDebugRights & rights) != rights) {
            return ERR_ACCESS_DENIED;
        }

        auto process = ProcessDispatcher::LookupProcessById(koid);
        if (!process)
            return ERR_NOT_FOUND;

        HandleOwner process_h(
            MakeHandle(mxtl::RefPtr<Dispatcher>(process.get()), rights));
        if (!process_h)
            return ERR_NO_MEMORY;

        if (make_user_ptr(_out).copy_to_user(up->MapHandleToValue(process_h)))
            return ERR_INVALID_ARGS;
        up->AddHandle(mxtl::move(process_h));
        return NO_ERROR;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t parent_rights;
    if (!up->GetDispatcher(handle, &dispatcher, &parent_rights))
        return ERR_BAD_HANDLE;

    if (!(parent_rights & MX_RIGHT_ENUMERATE))
        return ERR_ACCESS_DENIED;

    if (rights == MX_RIGHT_SAME_RIGHTS) {
        rights = parent_rights;
    } else if ((parent_rights & rights) != rights) {
        return ERR_ACCESS_DENIED;
    }

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        auto thread = process->LookupThreadById(koid);
        if (!thread)
            return ERR_NOT_FOUND;
        auto td = mxtl::RefPtr<Dispatcher>(thread->dispatcher());
        if (!td)
            return ERR_NOT_FOUND;
        HandleOwner thread_h(MakeHandle(td, rights));
        if (!thread_h)
            return ERR_NO_MEMORY;

        if (make_user_ptr(_out).copy_to_user(up->MapHandleToValue(thread_h)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        up->AddHandle(mxtl::move(thread_h));
        return NO_ERROR;
    }

    auto resource = DownCastDispatcher<ResourceDispatcher>(&dispatcher);
    if (resource) {
        auto child = resource->LookupChildById(koid);
        if (!child)
            return ERR_NOT_FOUND;
        auto cd = mxtl::RefPtr<Dispatcher>(child.get());
        if (!cd)
            return ERR_NOT_FOUND;
        HandleOwner child_h(MakeHandle(cd, rights));
        if (!child_h)
            return ERR_NO_MEMORY;

        if (make_user_ptr(_out).copy_to_user(up->MapHandleToValue(child_h)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        up->AddHandle(mxtl::move(child_h));
        return NO_ERROR;
    }

    return ERR_WRONG_TYPE;
}
