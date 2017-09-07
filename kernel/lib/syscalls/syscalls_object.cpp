// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/mp.h>
#include <kernel/stats.h>
#include <vm/pmm.h>
#include <lib/heap.h>
#include <platform.h>

#include <object/diagnostics.h>
#include <object/handle_owner.h>
#include <object/handles.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <object/resources.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>

#include <fbl/ref_ptr.h>

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
            if (ptr_.copy_array_to_user(&koid, 1, count_) != MX_OK) {
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

mx_status_t single_record_result(user_ptr<void> _buffer, size_t buffer_size,
                                 user_ptr<size_t> _actual,
                                 user_ptr<size_t> _avail,
                                 void* record_data, size_t record_size) {
    size_t avail = 1;
    size_t actual;
    if (buffer_size >= record_size) {
        if (_buffer.copy_array_to_user(record_data, record_size) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        actual = 1;
    } else {
        actual = 0;
    }
    if (_actual && (_actual.copy_to_user(actual) != MX_OK))
        return MX_ERR_INVALID_ARGS;
    if (_avail && (_avail.copy_to_user(avail) != MX_OK))
        return MX_ERR_INVALID_ARGS;
    if (actual == 0)
        return MX_ERR_BUFFER_TOO_SMALL;
    return MX_OK;
}

} // namespace

// actual is an optional return parameter for the number of records returned
// avail is an optional return parameter for the number of records available

// Topics which return a fixed number of records will return MX_ERR_BUFFER_TOO_SMALL
// if there is not enough buffer space provided.
// This allows for mx_object_get_info(handle, topic, &info, sizeof(info), NULL, NULL)

mx_status_t sys_object_get_info(mx_handle_t handle, uint32_t topic,
                                user_ptr<void> _buffer, size_t buffer_size,
                                user_ptr<size_t> _actual, user_ptr<size_t> _avail) {
    LTRACEF("handle %x topic %u\n", handle, topic);

    ProcessDispatcher* up = ProcessDispatcher::GetCurrent();

    switch (topic) {
        case MX_INFO_HANDLE_VALID: {
            return up->IsHandleValid(handle) ?  MX_OK : MX_ERR_BAD_HANDLE;
        }
        case MX_INFO_HANDLE_BASIC: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.

            fbl::RefPtr<Dispatcher> dispatcher;
            mx_rights_t rights;
            auto status = up->GetDispatcherAndRights(handle, &dispatcher, &rights);
            if (status != MX_OK)
                return status;

            bool waitable = dispatcher->get_state_tracker() != nullptr;

            // build the info structure
            mx_info_handle_basic_t info = {
                .koid = dispatcher->get_koid(),
                .rights = rights,
                .type = dispatcher->get_type(),
                .related_koid = dispatcher->get_related_koid(),
                .props = waitable ? MX_OBJ_PROP_WAITABLE : MX_OBJ_PROP_NONE,
            };

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
        }
        case MX_INFO_PROCESS: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.

            // grab a reference to the dispatcher
            fbl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &process);
            if (error < 0)
                return error;

            // build the info structure
            mx_info_process_t info = { };

            auto err = process->GetInfo(&info);
            if (err != MX_OK)
                return err;

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
        }
        case MX_INFO_PROCESS_THREADS: {
            // grab a reference to the dispatcher
            fbl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_ENUMERATE, &process);
            if (error < 0)
                return error;

            // Getting the list of threads is inherently racy (unless the
            // caller has already stopped all threads, but that's not our
            // concern). Still, we promise to either return all threads we know
            // about at a particular point in time, or notify the caller that
            // more threads exist than what we computed at that same point in
            // time.

            fbl::Array<mx_koid_t> threads;
            mx_status_t status = process->GetThreads(&threads);
            if (status != MX_OK)
                return status;
            size_t num_threads = threads.size();
            size_t num_space_for = buffer_size / sizeof(mx_koid_t);
            size_t num_to_copy = MIN(num_threads, num_space_for);

            // Don't try to copy if there are no bytes to copy, as the "is
            // user space" check may not handle (_buffer == NULL and len == 0).
            if (num_to_copy &&
                _buffer.copy_array_to_user(threads.get(), sizeof(mx_koid_t) * num_to_copy) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            if (_actual && (_actual.copy_to_user(num_to_copy) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(num_threads) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            return MX_OK;
        }
        case MX_INFO_JOB_CHILDREN:
        case MX_INFO_JOB_PROCESSES: {
            fbl::RefPtr<JobDispatcher> job;
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
                return MX_ERR_INVALID_ARGS;
            }
            if (_actual && (_actual.copy_to_user(sje.get_count()) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(sje.get_avail()) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            return MX_OK;
        }
        case MX_INFO_THREAD: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.

            // grab a reference to the dispatcher
            fbl::RefPtr<ThreadDispatcher> thread;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &thread);
            if (error < 0)
                return error;

            // build the info structure
            mx_info_thread_t info = { };

            auto err = thread->GetInfoForUserspace(&info);
            if (err != MX_OK)
                return err;

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
        }
        case MX_INFO_THREAD_EXCEPTION_REPORT: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.

            // grab a reference to the dispatcher
            fbl::RefPtr<ThreadDispatcher> thread;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &thread);
            if (error < 0)
                return error;

            // build the info structure
            mx_exception_report_t report = { };

            auto err = thread->GetExceptionReport(&report);
            if (err != MX_OK)
                return err;

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &report, sizeof(report));
        }
        case MX_INFO_THREAD_STATS: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.

            // grab a reference to the dispatcher
            fbl::RefPtr<ThreadDispatcher> thread;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &thread);
            if (error < 0)
                return error;

            // build the info structure
            mx_info_thread_stats_t info = { };

            auto err = thread->GetStatsForUserspace(&info);
            if (err != MX_OK)
                return err;

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
        }
        case MX_INFO_TASK_STATS: {
            // TODO(MG-458): Handle forward/backward compatibility issues
            // with changes to the struct.

            // Grab a reference to the dispatcher. Only supports processes for
            // now, but could support jobs or threads in the future.
            fbl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_READ,
                                                     &process);
            if (error < 0)
                return error;

            // Build the info structure.
            mx_info_task_stats_t info = {};

            auto err = process->GetStats(&info);
            if (err != MX_OK)
                return err;

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
        }
        case MX_INFO_PROCESS_MAPS: {
            fbl::RefPtr<ProcessDispatcher> process;
            mx_status_t status =
                up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &process);
            if (status < 0)
                return status;
            if (process.get() == up) {
                // Not safe to look at yourself: the user buffer will live
                // inside the VmAspace we're examining, and we can't
                // fault in the buffer's pages while the aspace lock is held.
                return MX_ERR_ACCESS_DENIED;
            }

            auto maps = _buffer.reinterpret<mx_info_maps_t>();
            size_t count = buffer_size / sizeof(mx_info_maps_t);
            size_t avail = 0;
            status = process->GetAspaceMaps(maps, count, &count, &avail);

            if (_actual && (_actual.copy_to_user(count) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            return status;
        }
        case MX_INFO_PROCESS_VMOS: {
            fbl::RefPtr<ProcessDispatcher> process;
            mx_status_t status =
                up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &process);
            if (status < 0)
                return status;
            if (process.get() == up) {
                // Not safe to look at yourself: the user buffer will live
                // inside the VmAspace we're examining, and we can't
                // fault in the buffer's pages while the aspace lock is held.
                return MX_ERR_ACCESS_DENIED;
            }

            auto vmos = _buffer.reinterpret<mx_info_vmo_t>();
            size_t count = buffer_size / sizeof(mx_info_vmo_t);
            size_t avail = 0;
            status = process->GetVmos(vmos, count, &count, &avail);

            if (_actual && (_actual.copy_to_user(count) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(avail) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            return status;
        }
        case MX_INFO_VMAR: {
            fbl::RefPtr<VmAddressRegionDispatcher> vmar;
            mx_status_t status = up->GetDispatcher(handle, &vmar);
            if (status < 0)
                return status;

            auto real_vmar = vmar->vmar();
            mx_info_vmar_t info = {
                .base = real_vmar->base(),
                .len = real_vmar->size(),
            };

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
        }
        case MX_INFO_CPU_STATS: {
            auto status = validate_resource(handle, MX_RSRC_KIND_ROOT);
            if (status != MX_OK)
                return status;

            // TODO: figure out a better handle to hang this off to and push this copy code into
            // that dispatcher.

            size_t num_cpus = arch_max_num_cpus();
            size_t num_space_for = buffer_size / sizeof(mx_info_cpu_stats_t);
            size_t num_to_copy = MIN(num_cpus, num_space_for);

            // build an alias to the output buffer that is in units of the cpu stat structure
            user_ptr<mx_info_cpu_stats_t> cpu_buf(static_cast<mx_info_cpu_stats_t *>(_buffer.get()));

            for (unsigned int i = 0; i < static_cast<unsigned int>(num_to_copy); i++) {
                const auto cpu = &percpu[i];

                // copy the per cpu stats from the kernel percpu structure
                // NOTE: it's technically racy to read this without grabbing a lock
                // but since each field is wordwise any sane architecture will not
                // return a corrupted value.
                mx_info_cpu_stats_t stats = {};
                stats.cpu_number = i;
                stats.flags = mp_is_cpu_online(i) ? MX_INFO_CPU_STATS_FLAG_ONLINE : 0;

                // account for idle time if a cpu is currently idle
                {
                    AutoSpinLockIrqSave lock(&thread_lock);

                    mx_time_t idle_time = cpu->stats.idle_time;
                    bool is_idle = mp_is_cpu_idle(i);
                    if (is_idle) {
                        idle_time += current_time() - percpu[i].idle_thread.last_started_running;
                    }
                    stats.idle_time = idle_time;
                }

                stats.reschedules = cpu->stats.reschedules;
                stats.context_switches = cpu->stats.context_switches;
                stats.irq_preempts = cpu->stats.irq_preempts;
                stats.preempts = cpu->stats.preempts;
                stats.yields = cpu->stats.yields;
                stats.ints = cpu->stats.interrupts;
                stats.timer_ints = cpu->stats.timer_ints;
                stats.timers = cpu->stats.timers;
                stats.page_faults = cpu->stats.page_faults;
                stats.exceptions = cpu->stats.exceptions;
                stats.syscalls = cpu->stats.syscalls;
                stats.reschedule_ipis = cpu->stats.reschedule_ipis;
                stats.generic_ipis = cpu->stats.generic_ipis;

                // copy out one at a time
                if (cpu_buf.copy_array_to_user(&stats, 1, i) != MX_OK)
                    return MX_ERR_INVALID_ARGS;
            }

            if (_actual && (_actual.copy_to_user(num_to_copy) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            if (_avail && (_avail.copy_to_user(num_cpus) != MX_OK))
                return MX_ERR_INVALID_ARGS;
            return MX_OK;
        }
        case MX_INFO_KMEM_STATS: {
            auto status = validate_resource(handle, MX_RSRC_KIND_ROOT);
            if (status != MX_OK)
                return status;

            // TODO: figure out a better handle to hang this off to and push this copy code into
            // that dispatcher.

            size_t state_count[_VM_PAGE_STATE_COUNT] = {};
            pmm_count_total_states(state_count);

            size_t total = 0;
            for (int i = 0; i < _VM_PAGE_STATE_COUNT; i++) {
                total += state_count[i];
            }

            size_t unused_size = 0;
            size_t free_heap_bytes = 0;
            heap_get_info(&unused_size, &free_heap_bytes);

            // Note that this intentionally uses uint64_t instead of
            // size_t in case we ever have a 32-bit userspace but more
            // than 4GB physical memory.
            mx_info_kmem_stats_t stats = {};
            stats.total_bytes = total * PAGE_SIZE;
            size_t other_bytes = stats.total_bytes;

            stats.free_bytes = state_count[VM_PAGE_STATE_FREE] * PAGE_SIZE;
            other_bytes -= stats.free_bytes;

            stats.wired_bytes = state_count[VM_PAGE_STATE_WIRED] * PAGE_SIZE;
            other_bytes -= stats.wired_bytes;

            stats.total_heap_bytes = state_count[VM_PAGE_STATE_HEAP] * PAGE_SIZE;
            other_bytes -= stats.total_heap_bytes;
            stats.free_heap_bytes = free_heap_bytes;

            stats.vmo_bytes = state_count[VM_PAGE_STATE_OBJECT] * PAGE_SIZE;
            other_bytes -= stats.vmo_bytes;

            stats.mmu_overhead_bytes = state_count[VM_PAGE_STATE_MMU] * PAGE_SIZE;
            other_bytes -= stats.mmu_overhead_bytes;

            // All other VM_PAGE_STATE_* counts get lumped into other_bytes.
            stats.other_bytes = other_bytes;

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &stats, sizeof(stats));
        }
        case MX_INFO_RESOURCE: {
            // grab a reference to the dispatcher
            fbl::RefPtr<ResourceDispatcher> resource;
            auto error = up->GetDispatcherWithRights(handle, MX_RIGHT_NONE, &resource);
            if (error < 0)
                return error;

            // build the info structure
            mx_info_resource_t info = {};
            info.kind = resource->get_kind();
            resource->get_range(&info.low, &info.high);

            return single_record_result(
                _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
        }
        default:
            return MX_ERR_NOT_SUPPORTED;
    }
}

mx_status_t sys_object_get_property(mx_handle_t handle_value, uint32_t property,
                                    user_ptr<void> _value, size_t size) {
    if (!_value)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcherWithRights(handle_value, MX_RIGHT_GET_PROPERTY, &dispatcher);
    if (status != MX_OK)
        return status;

    switch (property) {
        case MX_PROP_NUM_STATE_KINDS: {
            if (size != sizeof(uint32_t))
                return MX_ERR_BUFFER_TOO_SMALL;
            auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
            if (!thread)
                return MX_ERR_WRONG_TYPE;
            uint32_t value = thread->get_num_state_kinds();
            if (_value.reinterpret<uint32_t>().copy_to_user(value) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            return MX_OK;
        }
        case MX_PROP_NAME: {
            if (size < MX_MAX_NAME_LEN)
                return MX_ERR_BUFFER_TOO_SMALL;
            char name[MX_MAX_NAME_LEN];
            dispatcher->get_name(name);
            if (_value.copy_array_to_user(name, MX_MAX_NAME_LEN) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            return MX_OK;
        }
        case MX_PROP_PROCESS_DEBUG_ADDR: {
            if (size < sizeof(uintptr_t))
                return MX_ERR_BUFFER_TOO_SMALL;
            auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
            if (!process)
                return MX_ERR_WRONG_TYPE;
            uintptr_t value = process->get_debug_addr();
            if (_value.reinterpret<uintptr_t>().copy_to_user(value) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            return MX_OK;
        }
        case MX_PROP_PROCESS_VDSO_BASE_ADDRESS: {
            if (size < sizeof(uintptr_t))
                return MX_ERR_BUFFER_TOO_SMALL;
            auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
            if (!process)
                return MX_ERR_WRONG_TYPE;
            uintptr_t value = process->aspace()->vdso_base_address();
            return _value.reinterpret<uintptr_t>().copy_to_user(value);
        }
        case MX_PROP_JOB_IMPORTANCE: {
            if (size != sizeof(mx_job_importance_t))
                return MX_ERR_BUFFER_TOO_SMALL;
            auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
            if (!job)
                return MX_ERR_WRONG_TYPE;
            mx_job_importance_t value;
            mx_status_t status = job->get_importance(&value);
            if (status != MX_OK) {
                // Usually a problem resolving inherited importance,
                // like racing with task death.
                return status;
            }
            if (_value.reinterpret<mx_job_importance_t>()
                    .copy_to_user(value) != MX_OK) {
                return MX_ERR_INVALID_ARGS;
            }
            return MX_OK;
        }
        default:
            return MX_ERR_INVALID_ARGS;
    }

    __UNREACHABLE;
}

static mx_status_t is_current_thread(fbl::RefPtr<Dispatcher>* dispatcher) {
    auto thread_dispatcher = DownCastDispatcher<ThreadDispatcher>(dispatcher);
    if (!thread_dispatcher)
        return MX_ERR_WRONG_TYPE;
    if (thread_dispatcher.get() != ThreadDispatcher::GetCurrent())
        return MX_ERR_ACCESS_DENIED;
    return MX_OK;
}

mx_status_t sys_object_set_property(mx_handle_t handle_value, uint32_t property,
                                    user_ptr<const void> _value, size_t size) {
    if (!_value)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcherWithRights(handle_value, MX_RIGHT_SET_PROPERTY, &dispatcher);
    if (status != MX_OK)
        return status;

    switch (property) {
        case MX_PROP_NAME: {
            if (size >= MX_MAX_NAME_LEN)
                size = MX_MAX_NAME_LEN - 1;
            char name[MX_MAX_NAME_LEN - 1];
            if (_value.copy_array_from_user(name, size) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            return dispatcher->set_name(name, size);
        }
#if ARCH_X86_64
        case MX_PROP_REGISTER_FS: {
            if (size < sizeof(uintptr_t))
                return MX_ERR_BUFFER_TOO_SMALL;
            mx_status_t status = is_current_thread(&dispatcher);
            if (status != MX_OK)
                return status;
            uintptr_t addr;
            if (_value.reinterpret<const uintptr_t>().copy_from_user(&addr) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            if (!x86_is_vaddr_canonical(addr))
                return MX_ERR_INVALID_ARGS;
            write_msr(X86_MSR_IA32_FS_BASE, addr);
            return MX_OK;
        }
#endif
        case MX_PROP_PROCESS_DEBUG_ADDR: {
            if (size < sizeof(uintptr_t))
                return MX_ERR_BUFFER_TOO_SMALL;
            auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
            if (!process)
                return MX_ERR_WRONG_TYPE;
            uintptr_t value = 0;
            if (_value.reinterpret<const uintptr_t>().copy_from_user(&value) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            return process->set_debug_addr(value);
        }
        case MX_PROP_JOB_IMPORTANCE: {
            if (size != sizeof(mx_job_importance_t))
                return MX_ERR_BUFFER_TOO_SMALL;
            auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
            if (!job)
                return MX_ERR_WRONG_TYPE;
            int32_t value = 0;
            if (_value.reinterpret<const mx_job_importance_t>()
                    .copy_from_user(&value) != MX_OK) {
                return MX_ERR_INVALID_ARGS;
            }
            return job->set_importance(
                static_cast<mx_job_importance_t>(value));
        }
    }

    return MX_ERR_INVALID_ARGS;
}

mx_status_t sys_object_signal(mx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcherWithRights(handle_value, MX_RIGHT_SIGNAL, &dispatcher);
    if (status != MX_OK)
        return status;

    return dispatcher->user_signal(clear_mask, set_mask, false);
}

mx_status_t sys_object_signal_peer(mx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcherWithRights(handle_value, MX_RIGHT_SIGNAL_PEER, &dispatcher);
    if (status != MX_OK)
        return status;

    return dispatcher->user_signal(clear_mask, set_mask, true);
}

// Given a kernel object with children objects, obtain a handle to the
// child specified by the provided kernel object id.
//
// MX_HANDLE_INVALID is currently treated as a "magic" handle used to
// obtain a process from "the system".
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
            return MX_ERR_ACCESS_DENIED;
        }

        auto process = ProcessDispatcher::LookupProcessById(koid);
        if (!process)
            return MX_ERR_NOT_FOUND;

        HandleOwner process_h(
            MakeHandle(fbl::RefPtr<Dispatcher>(process.get()), rights));
        if (!process_h)
            return MX_ERR_NO_MEMORY;

        if (_out.copy_to_user(up->MapHandleToValue(process_h)))
            return MX_ERR_INVALID_ARGS;
        up->AddHandle(fbl::move(process_h));
        return MX_OK;
    }

    fbl::RefPtr<Dispatcher> dispatcher;
    uint32_t parent_rights;
    auto status = up->GetDispatcherAndRights(handle, &dispatcher, &parent_rights);
    if (status != MX_OK)
        return status;

    if (!(parent_rights & MX_RIGHT_ENUMERATE))
        return MX_ERR_ACCESS_DENIED;

    if (rights == MX_RIGHT_SAME_RIGHTS) {
        rights = parent_rights;
    } else if ((parent_rights & rights) != rights) {
        return MX_ERR_ACCESS_DENIED;
    }

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        auto thread = process->LookupThreadById(koid);
        if (!thread)
            return MX_ERR_NOT_FOUND;
        HandleOwner thread_h(MakeHandle(thread, rights));
        if (!thread_h)
            return MX_ERR_NO_MEMORY;

        if (_out.copy_to_user(up->MapHandleToValue(thread_h)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        up->AddHandle(fbl::move(thread_h));
        return MX_OK;
    }

    auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
    if (job) {
        auto child = job->LookupJobById(koid);
        if (child) {
            HandleOwner child_h(MakeHandle(child, rights));
            if (!child_h)
                return MX_ERR_NO_MEMORY;

            if (_out.copy_to_user(up->MapHandleToValue(child_h)) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            up->AddHandle(fbl::move(child_h));
            return MX_OK;
        }
        auto proc = job->LookupProcessById(koid);
        if (proc) {
            HandleOwner child_h(MakeHandle(proc, rights));
            if (!child_h)
                return MX_ERR_NO_MEMORY;

            if (_out.copy_to_user(up->MapHandleToValue(child_h)) != MX_OK)
                return MX_ERR_INVALID_ARGS;
            up->AddHandle(fbl::move(child_h));
            return MX_OK;
        }
        return MX_ERR_NOT_FOUND;
    }

    return MX_ERR_WRONG_TYPE;
}


mx_status_t sys_object_set_cookie(mx_handle_t handle, mx_handle_t hscope, uint64_t cookie) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_koid_t scope = up->GetKoidForHandle(hscope);
    if (scope == MX_KOID_INVALID)
        return MX_ERR_BAD_HANDLE;

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != MX_OK)
        return status;

    StateTracker* st = dispatcher->get_state_tracker();
    if (st == nullptr)
        return MX_ERR_NOT_SUPPORTED;

    return st->SetCookie(dispatcher->get_cookie_jar(), scope, cookie);
}

mx_status_t sys_object_get_cookie(mx_handle_t handle, mx_handle_t hscope, user_ptr<uint64_t> _cookie) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_koid_t scope = up->GetKoidForHandle(hscope);
    if (scope == MX_KOID_INVALID)
        return MX_ERR_BAD_HANDLE;

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != MX_OK)
        return status;

    StateTracker* st = dispatcher->get_state_tracker();
    if (st == nullptr)
        return MX_ERR_NOT_SUPPORTED;

    uint64_t cookie;
    status = st->GetCookie(dispatcher->get_cookie_jar(), scope, &cookie);
    if (status != MX_OK)
        return status;

    if (_cookie.copy_to_user(cookie) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return MX_OK;
}
