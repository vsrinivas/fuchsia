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
#include <lib/heap.h>
#include <platform.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <zircon/types.h>

#include <object/bus_transaction_initiator_dispatcher.h>
#include <object/diagnostics.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <object/resource.h>
#include <object/socket_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>

#include <fbl/ref_ptr.h>

#include "priv.h"

#define LOCAL_TRACE 0

namespace {

// Gathers the koids of a job's descendants.
class SimpleJobEnumerator final : public JobEnumerator {
public:
    // If |job| is true, only records job koids; otherwise, only
    // records process koids.
    SimpleJobEnumerator(user_out_ptr<zx_koid_t> ptr, size_t max, bool jobs)
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

    bool RecordKoid(zx_koid_t koid) {
        avail_++;
        if (count_ < max_) {
            // TODO: accumulate batches and do fewer user copies
            if (ptr_.copy_array_to_user(&koid, 1, count_) != ZX_OK) {
                return false;
            }
            count_++;
        }
        return true;
    }

    const bool jobs_;
    const user_out_ptr<zx_koid_t> ptr_;
    const size_t max_;

    size_t count_ = 0;
    size_t avail_ = 0;
};

zx_status_t single_record_result(user_out_ptr<void> _buffer, size_t buffer_size,
                                 user_out_ptr<size_t> _actual,
                                 user_out_ptr<size_t> _avail,
                                 void* record_data, size_t record_size) {
    size_t avail = 1;
    size_t actual;
    if (buffer_size >= record_size) {
        if (_buffer.copy_array_to_user(record_data, record_size) != ZX_OK)
            return ZX_ERR_INVALID_ARGS;
        actual = 1;
    } else {
        actual = 0;
    }
    if (_actual) {
        zx_status_t status = _actual.copy_to_user(actual);
        if (status != ZX_OK)
            return status;
    }
    if (_avail) {
        zx_status_t status = _avail.copy_to_user(avail);
        if (status != ZX_OK)
            return status;
    }
    if (actual == 0)
        return ZX_ERR_BUFFER_TOO_SMALL;
    return ZX_OK;
}

} // namespace

// actual is an optional return parameter for the number of records returned
// avail is an optional return parameter for the number of records available

// Topics which return a fixed number of records will return ZX_ERR_BUFFER_TOO_SMALL
// if there is not enough buffer space provided.
// This allows for zx_object_get_info(handle, topic, &info, sizeof(info), NULL, NULL)

zx_status_t sys_object_get_info(zx_handle_t handle, uint32_t topic,
                                user_out_ptr<void> _buffer, size_t buffer_size,
                                user_out_ptr<size_t> _actual, user_out_ptr<size_t> _avail) {
    LTRACEF("handle %x topic %u\n", handle, topic);

    ProcessDispatcher* up = ProcessDispatcher::GetCurrent();

    switch (topic) {
    case ZX_INFO_HANDLE_VALID: {
        // This syscall + topic is excepted from the ZX_POL_BAD_HANDLE policy.
        return up->IsHandleValidNoPolicyCheck(handle) ? ZX_OK : ZX_ERR_BAD_HANDLE;
    }
    case ZX_INFO_HANDLE_BASIC: {
        // TODO(ZX-458): Handle forward/backward compatibility issues
        // with changes to the struct.

        fbl::RefPtr<Dispatcher> dispatcher;
        zx_rights_t rights;
        auto status = up->GetDispatcherAndRights(handle, &dispatcher, &rights);
        if (status != ZX_OK)
            return status;

        bool waitable = dispatcher->has_state_tracker();

        // build the info structure
        zx_info_handle_basic_t info = {
            .koid = dispatcher->get_koid(),
            .rights = rights,
            .type = dispatcher->get_type(),
            .related_koid = dispatcher->get_related_koid(),
            .props = waitable ? ZX_OBJ_PROP_WAITABLE : ZX_OBJ_PROP_NONE,
        };

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_PROCESS: {
        // TODO(ZX-458): Handle forward/backward compatibility issues
        // with changes to the struct.

        // grab a reference to the dispatcher
        fbl::RefPtr<ProcessDispatcher> process;
        auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &process);
        if (error < 0)
            return error;

        // build the info structure
        zx_info_process_t info = {};

        auto err = process->GetInfo(&info);
        if (err != ZX_OK)
            return err;

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_PROCESS_THREADS: {
        // grab a reference to the dispatcher
        fbl::RefPtr<ProcessDispatcher> process;
        auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_ENUMERATE, &process);
        if (error < 0)
            return error;

        // Getting the list of threads is inherently racy (unless the
        // caller has already stopped all threads, but that's not our
        // concern). Still, we promise to either return all threads we know
        // about at a particular point in time, or notify the caller that
        // more threads exist than what we computed at that same point in
        // time.

        fbl::Array<zx_koid_t> threads;
        zx_status_t status = process->GetThreads(&threads);
        if (status != ZX_OK)
            return status;
        size_t num_threads = threads.size();
        size_t num_space_for = buffer_size / sizeof(zx_koid_t);
        size_t num_to_copy = MIN(num_threads, num_space_for);

        // Don't try to copy if there are no bytes to copy, as the "is
        // user space" check may not handle (_buffer == NULL and len == 0).
        if (num_to_copy &&
            _buffer.copy_array_to_user(threads.get(), sizeof(zx_koid_t) * num_to_copy) != ZX_OK)
            return ZX_ERR_INVALID_ARGS;
        if (_actual) {
            zx_status_t status = _actual.copy_to_user(num_to_copy);
            if (status != ZX_OK)
                return status;
        }
        if (_avail) {
            zx_status_t status = _avail.copy_to_user(num_threads);
            if (status != ZX_OK)
                return status;
        }
        return ZX_OK;
    }
    case ZX_INFO_JOB_CHILDREN:
    case ZX_INFO_JOB_PROCESSES: {
        fbl::RefPtr<JobDispatcher> job;
        auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_ENUMERATE, &job);
        if (error < 0)
            return error;

        size_t max = buffer_size / sizeof(zx_koid_t);
        auto koids = _buffer.reinterpret<zx_koid_t>();
        SimpleJobEnumerator sje(koids, max, topic == ZX_INFO_JOB_CHILDREN);

        // Don't recurse; we only want the job's direct children.
        if (!job->EnumerateChildren(&sje, /* recurse */ false)) {
            // SimpleJobEnumerator only returns false when it can't
            // write to the user pointer.
            return ZX_ERR_INVALID_ARGS;
        }
        if (_actual) {
            zx_status_t status = _actual.copy_to_user(sje.get_count());
            if (status != ZX_OK)
                return status;
        }
        if (_avail) {
            zx_status_t status = _avail.copy_to_user(sje.get_avail());
            if (status != ZX_OK)
                return status;
        }
        return ZX_OK;
    }
    case ZX_INFO_THREAD: {
        // TODO(ZX-458): Handle forward/backward compatibility issues
        // with changes to the struct.

        // grab a reference to the dispatcher
        fbl::RefPtr<ThreadDispatcher> thread;
        auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &thread);
        if (error < 0)
            return error;

        // build the info structure
        zx_info_thread_t info = {};

        auto err = thread->GetInfoForUserspace(&info);
        if (err != ZX_OK)
            return err;

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_THREAD_EXCEPTION_REPORT: {
        // TODO(ZX-458): Handle forward/backward compatibility issues
        // with changes to the struct.

        // grab a reference to the dispatcher
        fbl::RefPtr<ThreadDispatcher> thread;
        auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &thread);
        if (error < 0)
            return error;

        // build the info structure
        zx_exception_report_t report = {};

        auto err = thread->GetExceptionReport(&report);
        if (err != ZX_OK)
            return err;

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &report, sizeof(report));
    }
    case ZX_INFO_THREAD_STATS: {
        // TODO(ZX-458): Handle forward/backward compatibility issues
        // with changes to the struct.

        // grab a reference to the dispatcher
        fbl::RefPtr<ThreadDispatcher> thread;
        auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &thread);
        if (error < 0)
            return error;

        // build the info structure
        zx_info_thread_stats_t info = {};

        auto err = thread->GetStatsForUserspace(&info);
        if (err != ZX_OK)
            return err;

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_TASK_STATS: {
        // TODO(ZX-458): Handle forward/backward compatibility issues
        // with changes to the struct.

        // Grab a reference to the dispatcher. Only supports processes for
        // now, but could support jobs or threads in the future.
        fbl::RefPtr<ProcessDispatcher> process;
        auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ,
                                                 &process);
        if (error < 0)
            return error;

        // Build the info structure.
        zx_info_task_stats_t info = {};

        auto err = process->GetStats(&info);
        if (err != ZX_OK)
            return err;

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_PROCESS_MAPS: {
        fbl::RefPtr<ProcessDispatcher> process;
        zx_status_t status =
            up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &process);
        if (status < 0)
            return status;
        if (process.get() == up) {
            // Not safe to look at yourself: the user buffer will live
            // inside the VmAspace we're examining, and we can't
            // fault in the buffer's pages while the aspace lock is held.
            return ZX_ERR_ACCESS_DENIED;
        }

        auto maps = _buffer.reinterpret<zx_info_maps_t>();
        size_t count = buffer_size / sizeof(zx_info_maps_t);
        size_t avail = 0;
        status = process->GetAspaceMaps(maps, count, &count, &avail);

        if (_actual) {
            zx_status_t status = _actual.copy_to_user(count);
            if (status != ZX_OK)
                return status;
        }
        if (_avail) {
            zx_status_t status = _avail.copy_to_user(avail);
            if (status != ZX_OK)
                return status;
        }
        return status;
    }
    case ZX_INFO_PROCESS_VMOS: {
        fbl::RefPtr<ProcessDispatcher> process;
        zx_status_t status =
            up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &process);
        if (status < 0)
            return status;
        if (process.get() == up) {
            // Not safe to look at yourself: the user buffer will live
            // inside the VmAspace we're examining, and we can't
            // fault in the buffer's pages while the aspace lock is held.
            return ZX_ERR_ACCESS_DENIED;
        }

        auto vmos = _buffer.reinterpret<zx_info_vmo_t>();
        size_t count = buffer_size / sizeof(zx_info_vmo_t);
        size_t avail = 0;
        status = process->GetVmos(vmos, count, &count, &avail);

        if (_actual) {
            zx_status_t status = _actual.copy_to_user(count);
            if (status != ZX_OK)
                return status;
        }
        if (_avail) {
            zx_status_t status = _avail.copy_to_user(avail);
            if (status != ZX_OK)
                return status;
        }
        return status;
    }
    case ZX_INFO_VMAR: {
        fbl::RefPtr<VmAddressRegionDispatcher> vmar;
        zx_status_t status = up->GetDispatcher(handle, &vmar);
        if (status < 0)
            return status;

        auto real_vmar = vmar->vmar();
        zx_info_vmar_t info = {
            .base = real_vmar->base(),
            .len = real_vmar->size(),
        };

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_CPU_STATS: {
        auto status = validate_resource(handle, ZX_RSRC_KIND_ROOT);
        if (status != ZX_OK)
            return status;

        // TODO: figure out a better handle to hang this off to and push this copy code into
        // that dispatcher.

        size_t num_cpus = arch_max_num_cpus();
        size_t num_space_for = buffer_size / sizeof(zx_info_cpu_stats_t);
        size_t num_to_copy = MIN(num_cpus, num_space_for);

        // build an alias to the output buffer that is in units of the cpu stat structure
        user_out_ptr<zx_info_cpu_stats_t> cpu_buf = _buffer.reinterpret<zx_info_cpu_stats_t>();

        for (unsigned int i = 0; i < static_cast<unsigned int>(num_to_copy); i++) {
            const auto cpu = &percpu[i];

            // copy the per cpu stats from the kernel percpu structure
            // NOTE: it's technically racy to read this without grabbing a lock
            // but since each field is wordwise any sane architecture will not
            // return a corrupted value.
            zx_info_cpu_stats_t stats = {};
            stats.cpu_number = i;
            stats.flags = mp_is_cpu_online(i) ? ZX_INFO_CPU_STATS_FLAG_ONLINE : 0;

            // account for idle time if a cpu is currently idle
            {
                AutoSpinLock lock(&thread_lock);

                zx_time_t idle_time = cpu->stats.idle_time;
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
            stats.exceptions = 0; // deprecated, use "k counters" for now.
            stats.syscalls = cpu->stats.syscalls;
            stats.reschedule_ipis = cpu->stats.reschedule_ipis;
            stats.generic_ipis = cpu->stats.generic_ipis;

            // copy out one at a time
            if (cpu_buf.copy_array_to_user(&stats, 1, i) != ZX_OK)
                return ZX_ERR_INVALID_ARGS;
        }

        if (_actual) {
            zx_status_t status = _actual.copy_to_user(num_to_copy);
            if (status != ZX_OK)
                return status;
        }
        if (_avail) {
            zx_status_t status = _avail.copy_to_user(num_cpus);
            if (status != ZX_OK)
                return status;
        }
        return ZX_OK;
    }
    case ZX_INFO_KMEM_STATS: {
        auto status = validate_resource(handle, ZX_RSRC_KIND_ROOT);
        if (status != ZX_OK)
            return status;

        // TODO: figure out a better handle to hang this off to and push this copy code into
        // that dispatcher.

        size_t state_count[VM_PAGE_STATE_COUNT_] = {};
        pmm_count_total_states(state_count);

        size_t total = 0;
        for (int i = 0; i < VM_PAGE_STATE_COUNT_; i++) {
            total += state_count[i];
        }

        size_t unused_size = 0;
        size_t free_heap_bytes = 0;
        heap_get_info(&unused_size, &free_heap_bytes);

        // Note that this intentionally uses uint64_t instead of
        // size_t in case we ever have a 32-bit userspace but more
        // than 4GB physical memory.
        zx_info_kmem_stats_t stats = {};
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
    case ZX_INFO_RESOURCE: {
        // grab a reference to the dispatcher
        fbl::RefPtr<ResourceDispatcher> resource;
        zx_status_t error = up->GetDispatcherWithRights(handle, ZX_RIGHT_NONE, &resource);
        if (error != ZX_OK) {
            return error;
        }

        // build the info structure
        zx_info_resource_t info = {};
        info.kind = resource->get_kind();
        info.base = resource->get_base();
        info.size = resource->get_size();
        info.flags = resource->get_flags();
        resource->get_name(info.name);

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_HANDLE_COUNT: {
        fbl::RefPtr<Dispatcher> dispatcher;
        auto status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &dispatcher);
        if (status != ZX_OK)
            return status;

        zx_info_handle_count_t info = {
            .handle_count = Handle::Count(fbl::move(dispatcher))};

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_BTI: {
        fbl::RefPtr<BusTransactionInitiatorDispatcher> dispatcher;
        auto status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &dispatcher);
        if (status != ZX_OK)
            return status;

        zx_info_bti_t info = {
            .minimum_contiguity = dispatcher->minimum_contiguity(),
            .aspace_size = dispatcher->aspace_size(),
        };

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }
    case ZX_INFO_PROCESS_HANDLE_STATS: {
        fbl::RefPtr<ProcessDispatcher> process;
        auto status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &process);
        if (status != ZX_OK)
            return status;

        zx_info_process_handle_stats_t info = {};
        static_assert(fbl::count_of(info.handle_count) >= ZX_OBJ_TYPE_LAST,
                      "Need room for each handle type.");

        process->ForEachHandle([&](zx_handle_t handle, zx_rights_t rights,
                                   const Dispatcher* dispatcher) {
            ++info.handle_count[dispatcher->get_type()];
            return ZX_OK;
        });

        return single_record_result(
            _buffer, buffer_size, _actual, _avail, &info, sizeof(info));
    }

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t sys_object_get_property(zx_handle_t handle_value, uint32_t property,
                                    user_out_ptr<void> _value, size_t size) {
    if (!_value)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_GET_PROPERTY, &dispatcher);
    if (status != ZX_OK)
        return status;

    switch (property) {
    case ZX_PROP_NAME: {
        if (size < ZX_MAX_NAME_LEN)
            return ZX_ERR_BUFFER_TOO_SMALL;
        char name[ZX_MAX_NAME_LEN] = {};
        dispatcher->get_name(name);
        if (_value.copy_array_to_user(name, ZX_MAX_NAME_LEN) != ZX_OK)
            return ZX_ERR_INVALID_ARGS;
        return ZX_OK;
    }
    case ZX_PROP_PROCESS_DEBUG_ADDR: {
        if (size < sizeof(uintptr_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
        if (!process)
            return ZX_ERR_WRONG_TYPE;
        uintptr_t value = process->get_debug_addr();
        return _value.reinterpret<uintptr_t>().copy_to_user(value);
    }
    case ZX_PROP_PROCESS_VDSO_BASE_ADDRESS: {
        if (size < sizeof(uintptr_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
        if (!process)
            return ZX_ERR_WRONG_TYPE;
        uintptr_t value = process->aspace()->vdso_base_address();
        return _value.reinterpret<uintptr_t>().copy_to_user(value);
    }
    case ZX_PROP_SOCKET_RX_BUF_MAX: {
        if (size < sizeof(size_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        auto socket = DownCastDispatcher<SocketDispatcher>(&dispatcher);
        if (!socket)
            return ZX_ERR_WRONG_TYPE;
        size_t value = socket->ReceiveBufferMax();
        return _value.reinterpret<size_t>().copy_to_user(value);
    }
    case ZX_PROP_SOCKET_RX_BUF_SIZE: {
        if (size < sizeof(size_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        auto socket = DownCastDispatcher<SocketDispatcher>(&dispatcher);
        if (!socket)
            return ZX_ERR_WRONG_TYPE;
        size_t value = socket->ReceiveBufferSize();
        return _value.reinterpret<size_t>().copy_to_user(value);
    }
    case ZX_PROP_SOCKET_TX_BUF_MAX: {
        if (size < sizeof(size_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        auto socket = DownCastDispatcher<SocketDispatcher>(&dispatcher);
        if (!socket)
            return ZX_ERR_WRONG_TYPE;
        size_t value = socket->TransmitBufferMax();
        return _value.reinterpret<size_t>().copy_to_user(value);
    }
    case ZX_PROP_SOCKET_TX_BUF_SIZE: {
        if (size < sizeof(size_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        auto socket = DownCastDispatcher<SocketDispatcher>(&dispatcher);
        if (!socket)
            return ZX_ERR_WRONG_TYPE;
        size_t value = socket->TransmitBufferSize();
        return _value.reinterpret<size_t>().copy_to_user(value);
    }
    case ZX_PROP_CHANNEL_TX_MSG_MAX: {
        if (size < sizeof(size_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        auto channel = DownCastDispatcher<ChannelDispatcher>(&dispatcher);
        if (channel == nullptr) {
            return ZX_ERR_WRONG_TYPE;
        }
        size_t depth = channel->TxMessageMax();
        return _value.reinterpret<size_t>().copy_to_user(depth);
    }
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    __UNREACHABLE;
}

static zx_status_t is_current_thread(fbl::RefPtr<Dispatcher>* dispatcher) {
    auto thread_dispatcher = DownCastDispatcher<ThreadDispatcher>(dispatcher);
    if (!thread_dispatcher)
        return ZX_ERR_WRONG_TYPE;
    if (thread_dispatcher.get() != ThreadDispatcher::GetCurrent())
        return ZX_ERR_ACCESS_DENIED;
    return ZX_OK;
}

zx_status_t sys_object_set_property(zx_handle_t handle_value, uint32_t property,
                                    user_in_ptr<const void> _value, size_t size) {
    if (!_value)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_SET_PROPERTY, &dispatcher);
    if (status != ZX_OK)
        return status;

    switch (property) {
    case ZX_PROP_NAME: {
        if (size >= ZX_MAX_NAME_LEN)
            size = ZX_MAX_NAME_LEN - 1;
        char name[ZX_MAX_NAME_LEN - 1];
        if (_value.copy_array_from_user(name, size) != ZX_OK)
            return ZX_ERR_INVALID_ARGS;
        return dispatcher->set_name(name, size);
    }
#if ARCH_X86
    case ZX_PROP_REGISTER_FS: {
        if (size < sizeof(uintptr_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        zx_status_t status = is_current_thread(&dispatcher);
        if (status != ZX_OK)
            return status;
        uintptr_t addr;
        status = _value.reinterpret<const uintptr_t>().copy_from_user(&addr);
        if (status != ZX_OK)
            return status;
        if (!x86_is_vaddr_canonical(addr))
            return ZX_ERR_INVALID_ARGS;
        if (!is_user_address(addr))
            return ZX_ERR_INVALID_ARGS;
        write_msr(X86_MSR_IA32_FS_BASE, addr);
        return ZX_OK;
    }
    case ZX_PROP_REGISTER_GS: {
        if (size < sizeof(uintptr_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        zx_status_t status = is_current_thread(&dispatcher);
        if (status != ZX_OK)
            return status;
        uintptr_t addr;
        status = _value.reinterpret<const uintptr_t>().copy_from_user(&addr);
        if (status != ZX_OK)
            return status;
        if (!x86_is_vaddr_canonical(addr))
            return ZX_ERR_INVALID_ARGS;
        if (!is_user_address(addr))
            return ZX_ERR_INVALID_ARGS;
        write_msr(X86_MSR_IA32_KERNEL_GS_BASE, addr);
        return ZX_OK;
    }
#endif
    case ZX_PROP_PROCESS_DEBUG_ADDR: {
        if (size < sizeof(uintptr_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
        if (!process)
            return ZX_ERR_WRONG_TYPE;
        uintptr_t value = 0;
        zx_status_t status = _value.reinterpret<const uintptr_t>().copy_from_user(&value);
        if (status != ZX_OK)
            return status;
        return process->set_debug_addr(value);
    }
    }

    return ZX_ERR_INVALID_ARGS;
}

zx_status_t sys_object_signal(zx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_SIGNAL, &dispatcher);
    if (status != ZX_OK)
        return status;

    return dispatcher->user_signal(clear_mask, set_mask, false);
}

zx_status_t sys_object_signal_peer(zx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<Dispatcher> dispatcher;

    auto status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_SIGNAL_PEER, &dispatcher);
    if (status != ZX_OK)
        return status;

    return dispatcher->user_signal(clear_mask, set_mask, true);
}

// Given a kernel object with children objects, obtain a handle to the
// child specified by the provided kernel object id.
zx_status_t sys_object_get_child(zx_handle_t handle, uint64_t koid,
                                 zx_rights_t rights, user_out_handle* out) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<Dispatcher> dispatcher;
    uint32_t parent_rights;
    auto status = up->GetDispatcherAndRights(handle, &dispatcher, &parent_rights);
    if (status != ZX_OK)
        return status;

    if (!(parent_rights & ZX_RIGHT_ENUMERATE))
        return ZX_ERR_ACCESS_DENIED;

    if (rights == ZX_RIGHT_SAME_RIGHTS) {
        rights = parent_rights;
    } else if ((parent_rights & rights) != rights) {
        return ZX_ERR_ACCESS_DENIED;
    }

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        auto thread = process->LookupThreadById(koid);
        if (!thread)
            return ZX_ERR_NOT_FOUND;
        return out->make(fbl::move(thread), rights);
    }

    auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
    if (job) {
        auto child = job->LookupJobById(koid);
        if (child)
            return out->make(fbl::move(child), rights);
        auto proc = job->LookupProcessById(koid);
        if (proc)
            return out->make(fbl::move(proc), rights);
        return ZX_ERR_NOT_FOUND;
    }

    return ZX_ERR_WRONG_TYPE;
}

zx_status_t sys_object_set_cookie(zx_handle_t handle, zx_handle_t hscope, uint64_t cookie) {
    auto up = ProcessDispatcher::GetCurrent();

    zx_koid_t scope = up->GetKoidForHandle(hscope);
    if (scope == ZX_KOID_INVALID)
        return ZX_ERR_BAD_HANDLE;

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != ZX_OK)
        return status;

    if (!dispatcher->has_state_tracker())
        return ZX_ERR_NOT_SUPPORTED;

    return dispatcher->SetCookie(dispatcher->get_cookie_jar(), scope, cookie);
}

zx_status_t sys_object_get_cookie(zx_handle_t handle, zx_handle_t hscope, user_out_ptr<uint64_t> _cookie) {
    auto up = ProcessDispatcher::GetCurrent();

    zx_koid_t scope = up->GetKoidForHandle(hscope);
    if (scope == ZX_KOID_INVALID)
        return ZX_ERR_BAD_HANDLE;

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != ZX_OK)
        return status;

    if (!dispatcher->has_state_tracker())
        return ZX_ERR_NOT_SUPPORTED;

    uint64_t cookie;
    status = dispatcher->GetCookie(dispatcher->get_cookie_jar(), scope, &cookie);
    if (status != ZX_OK)
        return status;

    status = _cookie.copy_to_user(cookie);
    if (status != ZX_OK)
        return status;

    return ZX_OK;
}
