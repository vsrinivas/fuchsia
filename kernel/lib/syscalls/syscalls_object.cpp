// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <magenta/handle_owner.h>
#include <magenta/job_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/resource_dispatcher.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/vm_address_region_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

namespace {
// Gathers the koids of a job's descendants.
class SimpleJobEnumerator final : public JobEnumerator {
public:
    // If |job| is true, only records job koids; otherwise, only
    // records process koids.
    SimpleJobEnumerator(user_ptr<mx_koid_t> ptr, size_t max, bool jobs)
        : jobs_(jobs), ptr_(ptr), max_(max) {}

    size_t get_avail() const { return avail_; }
    size_t get_count() const { return count_; }

private:
    bool OnJob(JobDispatcher* job) override {
        if (!jobs_) {
            return true;
        }
        return RecordKoid(job->get_koid());
    }

    bool OnProcess(ProcessDispatcher* proc) override {
        if (jobs_) {
            return true;
        }
        return RecordKoid(proc->get_koid());
    }

    bool RecordKoid(mx_koid_t koid) {
        avail_++;
        if (count_ < max_) {
            // TODO: accumulate batches and do fewer user copies
            if (ptr_.copy_array_to_user(&koid, 1, count_) != NO_ERROR) {
                return false;
            }
            count_++;
        }
        return true;
    }

    const bool jobs_;
    const user_ptr<mx_koid_t> ptr_;
    const size_t max_;

    size_t count_ = 0;
    size_t avail_ = 0;
};
} // namespace

// actual is an optional return parameter for the number of records returned
// avail is an optional return parameter for the number of records available

// Topics which return a fixed number of records will return ERR_BUFFER_TOO_SMALL
// if there is not enough buffer space provided.
// This allows for mx_object_get_info(handle, topic, &info, sizeof(info), NULL, NULL)

mx_status_t sys_object_get_info(mx_handle_t handle, uint32_t topic,
                                user_ptr<void> _buffer, size_t buffer_size,
                                user_ptr<size_t> _actual, user_ptr<size_t> _avail) {
    LTRACEF("handle %d topic %u\n", handle, topic);

    ProcessDispatcher* up = ProcessDispatcher::GetCurrent();

    switch (topic) {
        case MX_INFO_HANDLE_VALID: {
            return up->IsHandleValid(handle) ?  NO_ERROR : ERR_BAD_HANDLE;
        }
        case MX_INFO_HANDLE_BASIC: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.
            size_t actual = (buffer_size < sizeof(mx_info_handle_basic_t)) ? 0 : 1;
            size_t avail = 1;

            mxtl::RefPtr<Dispatcher> dispatcher;
            mx_rights_t rights;
            auto status = up->GetDispatcherAndRights(handle, &dispatcher, &rights);
            if (status != NO_ERROR)
                return status;

            if (actual > 0) {
                bool waitable = dispatcher->get_state_tracker() != nullptr;

                // build the info structure
                mx_info_handle_basic_t info = {
                    .koid = dispatcher->get_koid(),
                    .rights = rights,
                    .type = dispatcher->get_type(),
                    .related_koid = dispatcher->get_related_koid(),
                    .props = waitable ? MX_OBJ_PROP_WAITABLE : MX_OBJ_PROP_NONE,
                };

                if (_buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;
            }
            if (_actual && (_actual.copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
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
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &process);
            if (error < 0)
                return error;

            if (actual > 0) {
                // build the info structure
                mx_info_process_t info = { };

                auto err = process->GetInfo(&info);
                if (err != NO_ERROR)
                    return err;

                if (_buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;
            }
            if (_actual && (_actual.copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (actual == 0)
                return ERR_BUFFER_TOO_SMALL;
            return NO_ERROR;
        }
        case MX_INFO_PROCESS_THREADS: {
            // grab a reference to the dispatcher
            mxtl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_ENUMERATE, &process);
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

            // Don't try to copy if there are no bytes to copy, as the "is
            // user space" check may not handle (_buffer == NULL and len == 0).
            if (num_to_copy &&
                _buffer.copy_array_to_user(threads.get(), sizeof(mx_koid_t) * num_to_copy) != NO_ERROR)
                return ERR_INVALID_ARGS;
            if (_actual && (_actual.copy_to_user(num_to_copy) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(num_threads) != NO_ERROR))
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_INFO_JOB_CHILDREN:
        case MX_INFO_JOB_PROCESSES: {
            mxtl::RefPtr<JobDispatcher> job;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_ENUMERATE, &job);
            if (error < 0)
                return error;

            size_t max = buffer_size / sizeof(mx_koid_t);
            auto koids = _buffer.reinterpret<mx_koid_t>();
            SimpleJobEnumerator sje(koids, max, topic == MX_INFO_JOB_CHILDREN);

            // Don't recurse; we only want the job's direct children.
            if (!job->EnumerateChildren(&sje, /* recurse */ false)) {
                // SimpleJobEnumerator only returns false when it can't
                // write to the user pointer.
                return ERR_INVALID_ARGS;
            }
            if (_actual && (_actual.copy_to_user(sje.get_count()) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(sje.get_avail()) != NO_ERROR))
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_INFO_RESOURCE_CHILDREN:
        case MX_INFO_RESOURCE_RECORDS: {
            mxtl::RefPtr<ResourceDispatcher> resource;
            mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_ENUMERATE, &resource);
            if (status < 0)
                return status;

            auto records = _buffer.reinterpret<mx_rrec_t>();
            size_t count = buffer_size / sizeof(mx_rrec_t);
            size_t avail = 0;
            if (topic == MX_INFO_RESOURCE_CHILDREN) {
                status = resource->GetChildren(records, count, &count, &avail);
            } else {
                status = resource->GetRecords(records, count, &count, &avail);
            }

            if (_actual && (_actual.copy_to_user(count) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            return status;
        }
        case MX_INFO_THREAD: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.
            size_t actual = (buffer_size < sizeof(mx_info_thread_t)) ? 0 : 1;
            size_t avail = 1;

            // grab a reference to the dispatcher
            mxtl::RefPtr<ThreadDispatcher> thread;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &thread);
            if (error < 0)
                return error;

            if (actual > 0) {
                // build the info structure
                mx_info_thread_t info = { };

                auto err = thread->GetInfo(&info);
                if (err != NO_ERROR)
                    return err;

                if (_buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;
            }
            if (_actual && (_actual.copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (actual == 0)
                return ERR_BUFFER_TOO_SMALL;
            return NO_ERROR;
        }
        case MX_INFO_THREAD_EXCEPTION_REPORT: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.
            size_t actual = (buffer_size < sizeof(mx_exception_report_t)) ? 0 : 1;
            size_t avail = 1;

            // grab a reference to the dispatcher
            mxtl::RefPtr<ThreadDispatcher> thread;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &thread);
            if (error < 0)
                return error;

            if (actual > 0) {
                // build the info structure
                mx_exception_report_t report = { };

                auto err = thread->GetExceptionReport(&report);
                if (err != NO_ERROR)
                    return err;

                if (_buffer.copy_array_to_user(&report, sizeof(report)) != NO_ERROR)
                    return ERR_INVALID_ARGS;
            }
            if (_actual && (_actual.copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (actual == 0)
                return ERR_BUFFER_TOO_SMALL;
            return NO_ERROR;
        }
        case MX_INFO_THREAD_STATS: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.
            size_t actual = (buffer_size < sizeof(mx_info_thread_stats_t)) ? 0 : 1;
            size_t avail = 1;

            // grab a reference to the dispatcher
            mxtl::RefPtr<ThreadDispatcher> thread;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &thread);
            if (error < 0)
                return error;

            if (actual > 0) {
                // build the info structure
                mx_info_thread_stats_t info = { };

                auto err = thread->GetStats(&info);
                if (err != NO_ERROR)
                    return err;

                if (_buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;
            }
            if (_actual && (_actual.copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (actual == 0)
                return ERR_BUFFER_TOO_SMALL;
            return NO_ERROR;
        }
        case MX_INFO_TASK_STATS: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.
            size_t actual =
                (buffer_size < sizeof(mx_info_task_stats_t)) ? 0 : 1;
            size_t avail = 1;

            // Grab a reference to the dispatcher. Only supports processes for
            // now, but could support jobs or threads in the future.
            mxtl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ,
                                                     &process);
            if (error < 0)
                return error;

            if (actual > 0) {
                // Build the info structure.
                mx_info_task_stats_t info = {};

                auto err = process->GetStats(&info);
                if (err != NO_ERROR)
                    return err;

                if (_buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;
            }
            if (_actual && (_actual.copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (actual == 0)
                return ERR_BUFFER_TOO_SMALL;
            return NO_ERROR;
        }
        case MX_INFO_PROCESS_MAPS: {
            mxtl::RefPtr<ProcessDispatcher> process;
            mx_status_t status =
                up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &process);
            if (status < 0)
                return status;
            if (process.get() == up) {
                // Not safe to look at yourself: the user buffer will live
                // inside the VmAspace we're examining, and we can't
                // fault in the buffer's pages while the aspace lock is held.
                return ERR_ACCESS_DENIED;
            }

            auto maps = _buffer.reinterpret<mx_info_maps_t>();
            size_t count = buffer_size / sizeof(mx_info_maps_t);
            size_t avail = 0;
            status = process->GetAspaceMaps(maps, count, &count, &avail);

            if (_actual && (_actual.copy_to_user(count) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
                return ERR_INVALID_ARGS;
            return status;
        }
        case MX_INFO_VMAR: {
            mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
            mx_status_t status = up->GetDispatcher(handle, &vmar);
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
                if (_buffer.copy_array_to_user(&info, sizeof(info)) != NO_ERROR)
                    return ERR_INVALID_ARGS;

            }

            if (_actual && (_actual.copy_to_user(actual) != NO_ERROR))
                return ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != NO_ERROR))
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
                                    user_ptr<void> _value, size_t size) {
    if (!_value)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcherWithRights(handle_value, MX_RIGHT_GET_PROPERTY, &dispatcher);
    if (status != NO_ERROR)
        return status;

    switch (property) {
        case MX_PROP_NUM_STATE_KINDS: {
            if (size != sizeof(uint32_t))
                return ERR_BUFFER_TOO_SMALL;
            auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
            if (!thread)
                return ERR_WRONG_TYPE;
            uint32_t value = thread->thread()->get_num_state_kinds();
            if (_value.reinterpret<uint32_t>().copy_to_user(value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_PROP_NAME: {
            if (size < MX_MAX_NAME_LEN)
                return ERR_BUFFER_TOO_SMALL;
            char name[MX_MAX_NAME_LEN];
            dispatcher->get_name(name);
            if (_value.copy_array_to_user(name, MX_MAX_NAME_LEN) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_PROP_PROCESS_DEBUG_ADDR: {
            if (size < sizeof(uintptr_t))
                return ERR_BUFFER_TOO_SMALL;
            auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
            if (!process)
                return ERR_WRONG_TYPE;
            uintptr_t value = process->get_debug_addr();
            if (_value.reinterpret<uintptr_t>().copy_to_user(value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_PROP_PROCESS_VDSO_BASE_ADDRESS: {
            if (size < sizeof(uintptr_t))
                return ERR_BUFFER_TOO_SMALL;
            auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
            if (!process)
                return ERR_WRONG_TYPE;
            uintptr_t value = process->aspace()->vdso_base_address();
            return _value.reinterpret<uintptr_t>().copy_to_user(value);
        }
        case MX_PROP_JOB_MAX_HEIGHT: {
            if (size < sizeof(uint32_t))
                return ERR_BUFFER_TOO_SMALL;
            auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
            if (!job)
                return ERR_WRONG_TYPE;
            uint32_t value = job->max_height();
            return _value.reinterpret<uint32_t>().copy_to_user(value);
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
                                    user_ptr<const void> _value, size_t size) {
    if (!_value)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcherWithRights(handle_value, MX_RIGHT_SET_PROPERTY, &dispatcher);
    if (status != NO_ERROR)
        return status;

    switch (property) {
        case MX_PROP_NAME: {
            if (size >= MX_MAX_NAME_LEN)
                size = MX_MAX_NAME_LEN - 1;
            char name[MX_MAX_NAME_LEN - 1];
            if (_value.copy_array_from_user(name, size) != NO_ERROR)
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
            if (_value.reinterpret<const uintptr_t>().copy_from_user(&addr) != NO_ERROR)
                return ERR_INVALID_ARGS;
            if (!x86_is_vaddr_canonical(addr))
                return ERR_INVALID_ARGS;
            write_msr(X86_MSR_IA32_FS_BASE, addr);
            return NO_ERROR;
        }
#endif
        case MX_PROP_PROCESS_DEBUG_ADDR: {
            if (size < sizeof(uintptr_t))
                return ERR_BUFFER_TOO_SMALL;
            auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
            if (!process)
                return ERR_WRONG_TYPE;
            uintptr_t value = 0;
            if (_value.reinterpret<const uintptr_t>().copy_from_user(&value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return process->set_debug_addr(value);
        }
    }

    return ERR_INVALID_ARGS;
}

mx_status_t sys_object_signal(mx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcherWithRights(handle_value, MX_RIGHT_WRITE, &dispatcher);
    if (status != NO_ERROR)
        return status;

    return dispatcher->user_signal(clear_mask, set_mask, false);
}

mx_status_t sys_object_signal_peer(mx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcher(handle_value, &dispatcher);
    if (status != NO_ERROR)
        return status;

    return dispatcher->user_signal(clear_mask, set_mask, true);
}

// Given a kernel object with children objects, obtain a handle to the
// child specified by the provided kernel object id.
//
// MX_HANDLE_INVALID is currently treated as a "magic" handle used to
// object a process from "the system".
mx_status_t sys_object_get_child(mx_handle_t handle, uint64_t koid, mx_rights_t rights,
                                 user_ptr<mx_handle_t> _out) {
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

        if (_out.copy_to_user(up->MapHandleToValue(process_h)))
            return ERR_INVALID_ARGS;
        up->AddHandle(mxtl::move(process_h));
        return NO_ERROR;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t parent_rights;
    auto status = up->GetDispatcherAndRights(handle, &dispatcher, &parent_rights);
    if (status != NO_ERROR)
        return status;

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

        if (_out.copy_to_user(up->MapHandleToValue(thread_h)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        up->AddHandle(mxtl::move(thread_h));
        return NO_ERROR;
    }

    auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
    if (job) {
        auto child = job->LookupJobById(koid);
        if (child) {
            HandleOwner child_h(MakeHandle(child, rights));
            if (!child_h)
                return ERR_NO_MEMORY;

            if (_out.copy_to_user(up->MapHandleToValue(child_h)) != NO_ERROR)
                return ERR_INVALID_ARGS;
            up->AddHandle(mxtl::move(child_h));
            return NO_ERROR;
        }
        auto proc = job->LookupProcessById(koid);
        if (proc) {
            HandleOwner child_h(MakeHandle(proc, rights));
            if (!child_h)
                return ERR_NO_MEMORY;

            if (_out.copy_to_user(up->MapHandleToValue(child_h)) != NO_ERROR)
                return ERR_INVALID_ARGS;
            up->AddHandle(mxtl::move(child_h));
            return NO_ERROR;
        }
        return ERR_NOT_FOUND;
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

        if (_out.copy_to_user(up->MapHandleToValue(child_h)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        up->AddHandle(mxtl::move(child_h));
        return NO_ERROR;
    }

    return ERR_WRONG_TYPE;
}


mx_status_t sys_object_set_cookie(mx_handle_t handle, mx_handle_t hscope, uint64_t cookie) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_koid_t scope = up->GetKoidForHandle(hscope);
    if (scope == MX_KOID_INVALID)
        return ERR_BAD_HANDLE;

    mxtl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != NO_ERROR)
        return status;

    StateTracker* st = dispatcher->get_state_tracker();
    if (st == nullptr)
        return ERR_NOT_SUPPORTED;

    return st->SetCookie(dispatcher->get_cookie_jar(), scope, cookie);
}

mx_status_t sys_object_get_cookie(mx_handle_t handle, mx_handle_t hscope, user_ptr<uint64_t> _cookie) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_koid_t scope = up->GetKoidForHandle(hscope);
    if (scope == MX_KOID_INVALID)
        return ERR_BAD_HANDLE;

    mxtl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != NO_ERROR)
        return status;

    StateTracker* st = dispatcher->get_state_tracker();
    if (st == nullptr)
        return ERR_NOT_SUPPORTED;

    uint64_t cookie;
    status = st->GetCookie(dispatcher->get_cookie_jar(), scope, &cookie);
    if (status != NO_ERROR)
        return status;

    if (_cookie.copy_to_user(cookie) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}
