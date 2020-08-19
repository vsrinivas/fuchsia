// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/heap.h>
#include <platform.h>
#include <trace.h>
#include <zircon/syscalls/object.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <kernel/mp.h>
#include <kernel/stats.h>
#include <kernel/thread_lock.h>
#include <ktl/algorithm.h>
#include <ktl/iterator.h>
#include <object/bus_transaction_initiator_dispatcher.h>
#include <object/diagnostics.h>
#include <object/exception_dispatcher.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/msi_allocation_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resource.h>
#include <object/resource_dispatcher.h>
#include <object/socket_dispatcher.h>
#include <object/stream_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/timer_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <vm/pmm.h>
#include <vm/vm.h>

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

template <typename T>
inline T VmoInfoToVersion(const zx_info_vmo_t& vmo);

template <>
inline zx_info_vmo_t VmoInfoToVersion(const zx_info_vmo_t& vmo) {
  return vmo;
}

template <>
inline zx_info_vmo_v1_t VmoInfoToVersion(const zx_info_vmo_t& vmo) {
  zx_info_vmo_v1_t vmo_v1 = {};
  vmo_v1.koid = vmo.koid;
  memcpy(vmo_v1.name, vmo.name, sizeof(vmo.name));
  vmo_v1.size_bytes = vmo.size_bytes;
  vmo_v1.parent_koid = vmo.parent_koid;
  vmo_v1.num_children = vmo.num_children;
  vmo_v1.num_mappings = vmo.num_mappings;
  vmo_v1.share_count = vmo.share_count;
  vmo_v1.flags = vmo.flags;
  vmo_v1.committed_bytes = vmo.committed_bytes;
  vmo_v1.handle_rights = vmo.handle_rights;
  vmo_v1.cache_policy = vmo.cache_policy;
  return vmo_v1;
}

// Specialize the VmoInfoWriter to work for any T that is a subset of zx_info_vmo_t. This is
// currently true for v1 and v2 (v2 being the current version). Being a subset the full
// zx_info_vmo_t can just be casted and copied.
template <typename T>
class SubsetVmoInfoWriter : public VmoInfoWriter {
 public:
  SubsetVmoInfoWriter(user_out_ptr<T> out) : out_(out) {}
  ~SubsetVmoInfoWriter() = default;
  zx_status_t Write(const zx_info_vmo_t& vmo, size_t offset) override {
    T versioned_vmo = VmoInfoToVersion<T>(vmo);
    return out_.element_offset(offset + base_offset_).copy_to_user(versioned_vmo);
  }
  UserCopyCaptureFaultsResult WriteCaptureFaults(const zx_info_vmo_t& vmo, size_t offset) override {
    T versioned_vmo = VmoInfoToVersion<T>(vmo);
    return out_.element_offset(offset + base_offset_).copy_to_user_capture_faults(versioned_vmo);
  }
  void AddOffset(size_t offset) override { base_offset_ += offset; }

 private:
  static_assert(sizeof(T) <= sizeof(zx_info_vmo_t));
  user_out_ptr<T> out_;
  size_t base_offset_ = 0;
};

// Copies a single record, |src_record|, into the user buffer |dst_buffer| of size
// |dst_buffer_size|.
//
// If the copy succeeds, the value 1 is copied into |user_avail| and |user_actual| (if non-null).
//
// If the copy fails because the buffer it too small, |user_avail| and |user_actual| will receive
// the values 1 and 0 respectively (if non-null).
template <typename T>
zx_status_t single_record_result(user_out_ptr<void> dst_buffer, size_t dst_buffer_size,
                                 user_out_ptr<size_t> user_actual, user_out_ptr<size_t> user_avail,
                                 const T& src_record) {
  size_t avail = 1;
  size_t actual;
  if (dst_buffer_size >= sizeof(T)) {
    if (dst_buffer.reinterpret<T>().copy_to_user(src_record) != ZX_OK)
      return ZX_ERR_INVALID_ARGS;
    actual = 1;
  } else {
    actual = 0;
  }
  if (user_actual) {
    zx_status_t status = user_actual.copy_to_user(actual);
    if (status != ZX_OK)
      return status;
  }
  if (user_avail) {
    zx_status_t status = user_avail.copy_to_user(avail);
    if (status != ZX_OK)
      return status;
  }
  if (actual == 0)
    return ZX_ERR_BUFFER_TOO_SMALL;
  return ZX_OK;
}

}  // namespace

// actual is an optional return parameter for the number of records returned
// avail is an optional return parameter for the number of records available

// Topics which return a fixed number of records will return ZX_ERR_BUFFER_TOO_SMALL
// if there is not enough buffer space provided.
// This allows for zx_object_get_info(handle, topic, &info, sizeof(info), NULL, NULL)

// zx_status_t zx_object_get_info
zx_status_t sys_object_get_info(zx_handle_t handle, uint32_t topic, user_out_ptr<void> _buffer,
                                size_t buffer_size, user_out_ptr<size_t> _actual,
                                user_out_ptr<size_t> _avail) {
  LTRACEF("handle %x topic %u\n", handle, topic);

  ProcessDispatcher* up = ProcessDispatcher::GetCurrent();

  switch (topic) {
    case ZX_INFO_HANDLE_VALID: {
      // This syscall + topic is excepted from the ZX_POL_BAD_HANDLE policy.
      fbl::RefPtr<Dispatcher> generic_dispatcher;
      return up->GetDispatcherWithRightsNoPolicyCheck(handle, 0, &generic_dispatcher, nullptr);
    }
    case ZX_INFO_HANDLE_BASIC: {
      // TODO(ZX-458): Handle forward/backward compatibility issues
      // with changes to the struct.

      fbl::RefPtr<Dispatcher> dispatcher;
      zx_rights_t rights;
      auto status = up->GetDispatcherAndRights(handle, &dispatcher, &rights);
      if (status != ZX_OK)
        return status;

      // build the info structure
      zx_info_handle_basic_t info = {
          .koid = dispatcher->get_koid(),
          .rights = rights,
          .type = dispatcher->get_type(),
          .related_koid = dispatcher->get_related_koid(),
          .reserved = 0u,
          .padding1 = {},
      };

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_PROCESS: {
      // TODO(ZX-458): Handle forward/backward compatibility issues
      // with changes to the struct.

      // grab a reference to the dispatcher
      fbl::RefPtr<ProcessDispatcher> process;
      auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &process);
      if (error != ZX_OK)
        return error;

      // build the info structure
      zx_info_process_t info = {};
      process->GetInfo(&info);
      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_PROCESS_THREADS: {
      // grab a reference to the dispatcher
      fbl::RefPtr<ProcessDispatcher> process;
      auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_ENUMERATE, &process);
      if (error != ZX_OK)
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
      size_t num_to_copy = ktl::min(num_threads, num_space_for);

      // Don't try to copy if there are no bytes to copy, as the "is
      // user space" check may not handle (_buffer == NULL and len == 0).
      if (num_to_copy && _buffer.reinterpret<zx_koid_t>().copy_array_to_user(
                             threads.data(), num_to_copy) != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
      }
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
      if (error != ZX_OK)
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
      auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &thread);
      if (error != ZX_OK)
        return error;

      // build the info structure
      zx_info_thread_t info = {};

      auto err = thread->GetInfoForUserspace(&info);
      if (err != ZX_OK)
        return err;

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_THREAD_EXCEPTION_REPORT: {
      // TODO(ZX-458): Handle forward/backward compatibility issues
      // with changes to the struct.

      // grab a reference to the dispatcher
      fbl::RefPtr<ThreadDispatcher> thread;
      auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &thread);
      if (error != ZX_OK)
        return error;

      // build the info structure
      zx_exception_report_t report = {};

      auto err = thread->GetExceptionReport(&report);
      if (err != ZX_OK)
        return err;

      return single_record_result(_buffer, buffer_size, _actual, _avail, report);
    }
    case ZX_INFO_THREAD_STATS: {
      // TODO(ZX-458): Handle forward/backward compatibility issues
      // with changes to the struct.

      // grab a reference to the dispatcher
      fbl::RefPtr<ThreadDispatcher> thread;
      auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &thread);
      if (error != ZX_OK)
        return error;

      // build the info structure
      zx_info_thread_stats_t info = {};

      auto err = thread->GetStatsForUserspace(&info);
      if (err != ZX_OK)
        return err;

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_TASK_STATS: {
      // TODO(ZX-458): Handle forward/backward compatibility issues
      // with changes to the struct.

      // Grab a reference to the dispatcher. Only supports processes for
      // now, but could support jobs or threads in the future.
      fbl::RefPtr<ProcessDispatcher> process;
      auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &process);
      if (error != ZX_OK)
        return error;

      // Build the info structure.
      zx_info_task_stats_t info = {};

      auto err = process->GetStats(&info);
      if (err != ZX_OK)
        return err;

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_TASK_RUNTIME: {
      fbl::RefPtr<Dispatcher> dispatcher;
      auto err = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &dispatcher);
      if (err != ZX_OK) {
        return err;
      }

      zx_info_task_runtime_t info = {};

      if (auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher)) {
        err = thread->AccumulateRuntimeTo(&info);
        if (err != ZX_OK) {
          return err;
        }
      } else if (auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher)) {
        err = process->AccumulateRuntimeTo(&info);
        if (err != ZX_OK) {
          return err;
        }
      } else if (auto job = DownCastDispatcher<JobDispatcher>(&dispatcher)) {
        err = job->AccumulateRuntimeTo(&info);
        if (err != ZX_OK) {
          return err;
        }
      } else {
        return ZX_ERR_WRONG_TYPE;
      }

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_PROCESS_MAPS: {
      fbl::RefPtr<ProcessDispatcher> process;
      zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &process);
      if (status != ZX_OK)
        return status;

      auto maps = _buffer.reinterpret<zx_info_maps_t>();
      size_t count = buffer_size / sizeof(zx_info_maps_t);
      size_t avail = 0;
      status = process->GetAspaceMaps(up->aspace().get(), maps, count, &count, &avail);

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
    case ZX_INFO_PROCESS_VMOS_V1:
    case ZX_INFO_PROCESS_VMOS: {
      fbl::RefPtr<ProcessDispatcher> process;
      zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &process);
      if (status != ZX_OK)
        return status;

      size_t count = 0;
      size_t avail = 0;

      if (topic == ZX_INFO_PROCESS_VMOS_V1) {
        SubsetVmoInfoWriter<zx_info_vmo_v1_t> writer{_buffer.reinterpret<zx_info_vmo_v1_t>()};
        count = buffer_size / sizeof(zx_info_vmo_v1_t);
        status = process->GetVmos(up->aspace().get(), writer, count, &count, &avail);
      } else {
        SubsetVmoInfoWriter<zx_info_vmo_t> writer{_buffer.reinterpret<zx_info_vmo_t>()};
        count = buffer_size / sizeof(zx_info_vmo_t);
        status = process->GetVmos(up->aspace().get(), writer, count, &count, &avail);
      }

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
    case ZX_INFO_VMO_V1:
    case ZX_INFO_VMO: {
      // lookup the dispatcher from handle
      fbl::RefPtr<VmObjectDispatcher> vmo;
      zx_status_t status = up->GetDispatcher(handle, &vmo);
      if (status != ZX_OK)
        return status;
      zx_info_vmo_t entry = vmo->GetVmoInfo();
      if (topic == ZX_INFO_VMO_V1) {
        zx_info_vmo_v1_t versioned_vmo = VmoInfoToVersion<zx_info_vmo_v1_t>(entry);
        // The V1 layout is a subset of V2
        return single_record_result(_buffer, buffer_size, _actual, _avail, versioned_vmo);
      } else {
        return single_record_result(_buffer, buffer_size, _actual, _avail, entry);
      }
    }
    case ZX_INFO_VMAR: {
      fbl::RefPtr<VmAddressRegionDispatcher> vmar;
      zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &vmar);
      if (status != ZX_OK)
        return status;

      auto real_vmar = vmar->vmar();
      zx_info_vmar_t info = {
          .base = real_vmar->base(),
          .len = real_vmar->size(),
      };

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }

    case ZX_INFO_GUEST_STATS: {
      auto status = validate_resource(handle, ZX_RSRC_KIND_ROOT);
      if (status != ZX_OK)
        return status;

      size_t num_cpus = arch_max_num_cpus();
      size_t num_space_for = buffer_size / sizeof(zx_info_cpu_stats_t);
      size_t num_to_copy = ktl::min(num_cpus, num_space_for);

      user_out_ptr<zx_info_guest_stats_t> guest_buf = _buffer.reinterpret<zx_info_guest_stats_t>();

      for (unsigned int i = 0; i < static_cast<unsigned int>(num_to_copy); i++) {
        const auto* cpu = &percpu::Get(i);
        zx_info_guest_stats_t stats = {};
        stats.cpu_number = i;
        stats.flags = mp_is_cpu_online(i) ? ZX_INFO_CPU_STATS_FLAG_ONLINE : 0;

        stats.vm_entries = cpu->gstats.vm_entries;
        stats.vm_exits = cpu->gstats.vm_exits;
#ifdef __aarch64__
        stats.wfi_wfe_instructions = cpu->gstats.wfi_wfe_instructions;
        stats.system_instructions = cpu->gstats.system_instructions;
        stats.instruction_aborts = cpu->gstats.instruction_aborts;
        stats.data_aborts = cpu->gstats.data_aborts;
        stats.smc_instructions = cpu->gstats.smc_instructions;
        stats.interrupts = cpu->gstats.interrupts;
#else
        stats.vmcall_instructions = cpu->gstats.vmcall_instructions;
        stats.pause_instructions = cpu->gstats.pause_instructions;
        stats.xsetbv_instructions = cpu->gstats.xsetbv_instructions;
        stats.ept_violations = cpu->gstats.ept_violations;
        stats.wrmsr_instructions = cpu->gstats.wrmsr_instructions;
        stats.rdmsr_instructions = cpu->gstats.rdmsr_instructions;
        stats.io_instructions = cpu->gstats.io_instructions;
        stats.control_register_accesses = cpu->gstats.control_register_accesses;
        stats.hlt_instructions = cpu->gstats.hlt_instructions;
        stats.cpuid_instructions = cpu->gstats.cpuid_instructions;
        stats.interrupt_windows = cpu->gstats.interrupt_windows;
        stats.interrupts = cpu->gstats.interrupts;
#endif
        if (guest_buf.copy_array_to_user(&stats, 1, i) != ZX_OK)
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

    case ZX_INFO_CPU_STATS: {
      auto status = validate_resource(handle, ZX_RSRC_KIND_ROOT);
      if (status != ZX_OK)
        return status;

      // TODO: figure out a better handle to hang this off to and push this copy code into
      // that dispatcher.

      size_t num_cpus = arch_max_num_cpus();
      size_t num_space_for = buffer_size / sizeof(zx_info_cpu_stats_t);
      size_t num_to_copy = ktl::min(num_cpus, num_space_for);

      // build an alias to the output buffer that is in units of the cpu stat structure
      user_out_ptr<zx_info_cpu_stats_t> cpu_buf = _buffer.reinterpret<zx_info_cpu_stats_t>();

      for (unsigned int i = 0; i < static_cast<unsigned int>(num_to_copy); i++) {
        const auto* cpu = &percpu::Get(i);

        // copy the per cpu stats from the kernel percpu structure
        // NOTE: it's technically racy to read this without grabbing a lock
        // but since each field is wordwise any sane architecture will not
        // return a corrupted value.
        zx_info_cpu_stats_t stats = {};
        stats.cpu_number = i;
        stats.flags = mp_is_cpu_online(i) ? ZX_INFO_CPU_STATS_FLAG_ONLINE : 0;

        // account for idle time if a cpu is currently idle
        {
          Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

          zx_time_t idle_time = cpu->stats.idle_time;
          bool is_idle = mp_is_cpu_idle(i);
          if (is_idle) {
            zx_duration_t recent_idle = zx_time_sub_time(
                current_time(), cpu->idle_thread.scheduler_state().last_started_running());
            idle_time = zx_duration_add_duration(idle_time, recent_idle);
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
        stats.exceptions = 0;  // deprecated, use "kcounter" command for now.
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

      // |get_count| returns an estimate so the sum of the counts may not equal the total.
      uint64_t state_count[VM_PAGE_STATE_COUNT_] = {};
      for (uint32_t i = 0; i < VM_PAGE_STATE_COUNT_; i++) {
        state_count[i] = vm_page_t::get_count(vm_page_state(i));
      }

      uint64_t free_heap_bytes = 0;
      heap_get_info(nullptr, &free_heap_bytes);

      // Note that this intentionally uses uint64_t instead of
      // size_t in case we ever have a 32-bit userspace but more
      // than 4GB physical memory.
      zx_info_kmem_stats_t stats = {};
      stats.total_bytes = pmm_count_total_bytes();

      // Holds the sum of bytes in the broken out states. This sum could be less than the total
      // because we aren't counting all possible states (e.g. VM_PAGE_STATE_ALLOC). This sum could
      // be greater than the total because per-state counts are approximate.
      uint64_t sum_bytes = 0;

      stats.free_bytes = state_count[VM_PAGE_STATE_FREE] * PAGE_SIZE;
      sum_bytes += stats.free_bytes;

      stats.wired_bytes = state_count[VM_PAGE_STATE_WIRED] * PAGE_SIZE;
      sum_bytes += stats.wired_bytes;

      stats.total_heap_bytes = state_count[VM_PAGE_STATE_HEAP] * PAGE_SIZE;
      sum_bytes += stats.total_heap_bytes;
      stats.free_heap_bytes = free_heap_bytes;

      stats.vmo_bytes = state_count[VM_PAGE_STATE_OBJECT] * PAGE_SIZE;
      sum_bytes += stats.vmo_bytes;

      stats.mmu_overhead_bytes = state_count[VM_PAGE_STATE_MMU] * PAGE_SIZE;
      sum_bytes += stats.mmu_overhead_bytes;

      stats.ipc_bytes = state_count[VM_PAGE_STATE_IPC] * PAGE_SIZE;
      sum_bytes += stats.ipc_bytes;

      // Is there unaccounted memory?
      if (stats.total_bytes > sum_bytes) {
        // Everything else gets counted as "other".
        stats.other_bytes = stats.total_bytes - sum_bytes;
      } else {
        // One or more of our per-state counts may have been off. We'll ignore it.
        stats.other_bytes = 0;
      }

      return single_record_result(_buffer, buffer_size, _actual, _avail, stats);
    }
    case ZX_INFO_RESOURCE: {
      // grab a reference to the dispatcher
      fbl::RefPtr<ResourceDispatcher> resource;
      zx_status_t error = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &resource);
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

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_HANDLE_COUNT: {
      fbl::RefPtr<Dispatcher> dispatcher;
      auto status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &dispatcher);
      if (status != ZX_OK)
        return status;

      zx_info_handle_count_t info = {.handle_count = Handle::Count(ktl::move(dispatcher))};

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_BTI: {
      fbl::RefPtr<BusTransactionInitiatorDispatcher> dispatcher;
      auto status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &dispatcher);
      if (status != ZX_OK)
        return status;

      zx_info_bti_t info = {
          .minimum_contiguity = dispatcher->minimum_contiguity(),
          .aspace_size = dispatcher->aspace_size(),
          .pmo_count = dispatcher->pmo_count(),
          .quarantine_count = dispatcher->quarantine_count(),
      };

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }
    case ZX_INFO_PROCESS_HANDLE_STATS: {
      fbl::RefPtr<ProcessDispatcher> process;
      auto status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &process);
      if (status != ZX_OK)
        return status;

      zx_info_process_handle_stats_t info = {};
      static_assert(ktl::size(info.handle_count) >= ZX_OBJ_TYPE_UPPER_BOUND,
                    "Need room for each handle type.");

      process->ForEachHandle(
          [&](zx_handle_t handle, zx_rights_t rights, const Dispatcher* dispatcher) {
            ++info.handle_count[dispatcher->get_type()];
            return ZX_OK;
          });

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }

    case ZX_INFO_SOCKET: {
      fbl::RefPtr<SocketDispatcher> socket;
      auto status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &socket);
      if (status != ZX_OK)
        return status;

      zx_info_socket_t info = {};
      socket->GetInfo(&info);

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }

    case ZX_INFO_JOB: {
      fbl::RefPtr<JobDispatcher> job;
      auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &job);
      if (error != ZX_OK)
        return error;

      zx_info_job info = {};
      job->GetInfo(&info);

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }

    case ZX_INFO_TIMER: {
      fbl::RefPtr<TimerDispatcher> timer;
      auto error = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &timer);
      if (error != ZX_OK)
        return error;

      zx_info_timer info = {};
      timer->GetInfo(&info);

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }

    case ZX_INFO_STREAM: {
      fbl::RefPtr<StreamDispatcher> stream;
      zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &stream);
      if (status != ZX_OK) {
        return status;
      }

      zx_info_stream_t info = {};
      stream->GetInfo(&info);

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }

    case ZX_INFO_HANDLE_TABLE: {
      fbl::RefPtr<ProcessDispatcher> process;
      auto error = up->GetDispatcherWithRights(
          handle, ZX_RIGHT_INSPECT | ZX_RIGHT_MANAGE_PROCESS | ZX_RIGHT_MANAGE_THREAD, &process);
      if (error != ZX_OK)
        return error;

      if (!_buffer && !_avail && _actual) {
        // Optimization for callers which call twice, the first time just to know the size.
        return _actual.copy_to_user(static_cast<size_t>(up->HandleCount()));
      }

      fbl::Array<zx_info_handle_extended_t> handle_info;
      zx_status_t status = process->GetHandleInfo(&handle_info);
      if (status != ZX_OK)
        return status;

      size_t num_records = handle_info.size();
      size_t num_space_for = buffer_size / sizeof(zx_info_handle_extended_t);
      size_t num_to_copy = ktl::min(num_records, num_space_for);

      // Don't try to copy if there are no bytes to copy, as the "is
      // user space" check may not handle (_buffer == NULL and len == 0).
      if (num_to_copy && _buffer.reinterpret<zx_info_handle_extended_t>().copy_array_to_user(
                             handle_info.data(), num_to_copy) != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
      }
      if (_actual) {
        zx_status_t status = _actual.copy_to_user(num_to_copy);
        if (status != ZX_OK)
          return status;
      }
      if (_avail) {
        zx_status_t status = _avail.copy_to_user(num_records);
        if (status != ZX_OK)
          return status;
      }
      return ZX_OK;
    }
    case ZX_INFO_MSI: {
      fbl::RefPtr<MsiAllocationDispatcher> allocation;
      zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_INSPECT, &allocation);
      if (status != ZX_OK) {
        return status;
      }

      zx_info_msi_t info = {};
      allocation->GetInfo(&info);

      return single_record_result(_buffer, buffer_size, _actual, _avail, info);
    }

    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

// zx_status_t zx_object_get_property
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
      if (_value.reinterpret<char>().copy_array_to_user(name, ZX_MAX_NAME_LEN) != ZX_OK)
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
    case ZX_PROP_PROCESS_BREAK_ON_LOAD: {
      if (size < sizeof(uintptr_t))
        return ZX_ERR_BUFFER_TOO_SMALL;
      auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
      if (!process)
        return ZX_ERR_WRONG_TYPE;
      uintptr_t value = process->get_dyn_break_on_load();
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
    case ZX_PROP_PROCESS_HW_TRACE_CONTEXT_ID: {
      if (!DebuggingSyscallsEnabled()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
#if ARCH_X86
      if (size < sizeof(uintptr_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
      if (!process) {
        return ZX_ERR_WRONG_TYPE;
      }
      uintptr_t value = process->aspace()->arch_aspace().pt_phys();
      return _value.reinterpret<uintptr_t>().copy_to_user(value);
#else
      return ZX_ERR_NOT_SUPPORTED;
#endif
    }
    case ZX_PROP_SOCKET_RX_THRESHOLD: {
      if (size < sizeof(size_t))
        return ZX_ERR_BUFFER_TOO_SMALL;
      auto socket = DownCastDispatcher<SocketDispatcher>(&dispatcher);
      if (!socket)
        return ZX_ERR_WRONG_TYPE;
      size_t value = socket->GetReadThreshold();
      return _value.reinterpret<size_t>().copy_to_user(value);
    }
    case ZX_PROP_SOCKET_TX_THRESHOLD: {
      if (size < sizeof(size_t))
        return ZX_ERR_BUFFER_TOO_SMALL;
      auto socket = DownCastDispatcher<SocketDispatcher>(&dispatcher);
      if (!socket)
        return ZX_ERR_WRONG_TYPE;
      size_t value = socket->GetWriteThreshold();
      return _value.reinterpret<size_t>().copy_to_user(value);
    }
    case ZX_PROP_EXCEPTION_STATE: {
      if (size < sizeof(uint32_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto exception = DownCastDispatcher<ExceptionDispatcher>(&dispatcher);
      if (!exception) {
        return ZX_ERR_WRONG_TYPE;
      }

      bool resume_on_close = exception->ResumesThreadOnClose();
      return _value.reinterpret<uint32_t>().copy_to_user(
          resume_on_close ? ZX_EXCEPTION_STATE_HANDLED : ZX_EXCEPTION_STATE_TRY_NEXT);
    }
    case ZX_PROP_EXCEPTION_STRATEGY: {
      if (size < sizeof(uint32_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto exception = DownCastDispatcher<ExceptionDispatcher>(&dispatcher);
      if (!exception) {
        return ZX_ERR_WRONG_TYPE;
      }

      bool second_chance = exception->IsSecondChance();
      return _value.reinterpret<uint32_t>().copy_to_user(
          second_chance ? ZX_EXCEPTION_STRATEGY_SECOND_CHANCE : ZX_EXCEPTION_STRATEGY_FIRST_CHANCE);
    }
    case ZX_PROP_VMO_CONTENT_SIZE: {
      if (size < sizeof(uint64_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto vmo = DownCastDispatcher<VmObjectDispatcher>(&dispatcher);
      if (!vmo) {
        return ZX_ERR_WRONG_TYPE;
      }

      uint64_t value = vmo->GetContentSize();
      return _value.reinterpret<uint64_t>().copy_to_user(value);
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

// zx_status_t zx_object_set_property
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
      if (_value.reinterpret<const char>().copy_array_from_user(name, size) != ZX_OK)
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
    case ZX_PROP_PROCESS_BREAK_ON_LOAD: {
      if (size < sizeof(uintptr_t))
        return ZX_ERR_BUFFER_TOO_SMALL;
      auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
      if (!process)
        return ZX_ERR_WRONG_TYPE;
      uintptr_t value = 0;
      zx_status_t status = _value.reinterpret<const uintptr_t>().copy_from_user(&value);
      if (status != ZX_OK)
        return status;
      return process->set_dyn_break_on_load(value);
    }
    case ZX_PROP_SOCKET_RX_THRESHOLD: {
      if (size < sizeof(size_t))
        return ZX_ERR_BUFFER_TOO_SMALL;
      auto socket = DownCastDispatcher<SocketDispatcher>(&dispatcher);
      if (!socket)
        return ZX_ERR_WRONG_TYPE;
      size_t value = 0;
      zx_status_t status = _value.reinterpret<const size_t>().copy_from_user(&value);
      if (status != ZX_OK)
        return status;
      return socket->SetReadThreshold(value);
    }
    case ZX_PROP_SOCKET_TX_THRESHOLD: {
      if (size < sizeof(size_t))
        return ZX_ERR_BUFFER_TOO_SMALL;
      auto socket = DownCastDispatcher<SocketDispatcher>(&dispatcher);
      if (!socket)
        return ZX_ERR_WRONG_TYPE;
      size_t value = 0;
      zx_status_t status = _value.reinterpret<const size_t>().copy_from_user(&value);
      if (status != ZX_OK)
        return status;
      return socket->SetWriteThreshold(value);
    }
    case ZX_PROP_JOB_KILL_ON_OOM: {
      auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
      if (!job)
        return ZX_ERR_WRONG_TYPE;
      size_t value = 0;
      zx_status_t status = _value.reinterpret<const size_t>().copy_from_user(&value);
      if (status != ZX_OK)
        return status;
      if (value == 0u) {
        job->set_kill_on_oom(false);
      } else if (value == 1u) {
        job->set_kill_on_oom(true);
      } else {
        return ZX_ERR_INVALID_ARGS;
      }
      return ZX_OK;
    }
    case ZX_PROP_EXCEPTION_STATE: {
      if (size < sizeof(uint32_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto exception = DownCastDispatcher<ExceptionDispatcher>(&dispatcher);
      if (!exception) {
        return ZX_ERR_WRONG_TYPE;
      }
      uint32_t value = 0;
      zx_status_t status = _value.reinterpret<const uint32_t>().copy_from_user(&value);
      if (status != ZX_OK) {
        return status;
      }
      if (value == ZX_EXCEPTION_STATE_HANDLED) {
        exception->SetWhetherResumesThreadOnClose(true);
      } else if (value == ZX_EXCEPTION_STATE_TRY_NEXT) {
        exception->SetWhetherResumesThreadOnClose(false);
      } else {
        return ZX_ERR_INVALID_ARGS;
      }
      return ZX_OK;
    }
    case ZX_PROP_EXCEPTION_STRATEGY: {
      if (size < sizeof(uint32_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto exception = DownCastDispatcher<ExceptionDispatcher>(&dispatcher);
      if (!exception) {
        return ZX_ERR_WRONG_TYPE;
      }

      // Invalid if the exception handle is not held by a debugger.
      zx_info_thread_t info = {};
      zx_status_t status = exception->thread()->GetInfoForUserspace(&info);
      if (status != ZX_OK) {
        return status;
      }
      if (info.wait_exception_channel_type != ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER) {
        return ZX_ERR_BAD_STATE;
      }

      uint32_t value = 0;
      status = _value.reinterpret<const uint32_t>().copy_from_user(&value);
      if (status != ZX_OK) {
        return status;
      }
      if (value == ZX_EXCEPTION_STRATEGY_FIRST_CHANCE) {
        exception->SetWhetherSecondChance(false);
      } else if (value == ZX_EXCEPTION_STRATEGY_SECOND_CHANCE) {
        exception->SetWhetherSecondChance(true);
      } else {
        return ZX_ERR_INVALID_ARGS;
      }
      return ZX_OK;
    }
    case ZX_PROP_VMO_CONTENT_SIZE: {
      if (size < sizeof(uint64_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto vmo = DownCastDispatcher<VmObjectDispatcher>(&dispatcher);
      if (!vmo) {
        return ZX_ERR_WRONG_TYPE;
      }
      uint64_t value = 0;
      zx_status_t status = _value.reinterpret<const uint64_t>().copy_from_user(&value);
      if (status != ZX_OK) {
        return status;
      }
      return vmo->SetContentSize(value);
    }
  }

  return ZX_ERR_INVALID_ARGS;
}

// zx_status_t zx_object_signal
zx_status_t sys_object_signal(zx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
  LTRACEF("handle %x\n", handle_value);

  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<Dispatcher> dispatcher;

  auto status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_SIGNAL, &dispatcher);
  if (status != ZX_OK)
    return status;

  return dispatcher->user_signal_self(clear_mask, set_mask);
}

// zx_status_t zx_object_signal_peer
zx_status_t sys_object_signal_peer(zx_handle_t handle_value, uint32_t clear_mask,
                                   uint32_t set_mask) {
  LTRACEF("handle %x\n", handle_value);

  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<Dispatcher> dispatcher;

  auto status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_SIGNAL_PEER, &dispatcher);
  if (status != ZX_OK)
    return status;

  return dispatcher->user_signal_peer(clear_mask, set_mask);
}

// Given a kernel object with children objects, obtain a handle to the
// child specified by the provided kernel object id.
// zx_status_t zx_object_get_child
zx_status_t sys_object_get_child(zx_handle_t handle, uint64_t koid, zx_rights_t rights,
                                 user_out_handle* out) {
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
    return out->make(ktl::move(thread), rights);
  }

  auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
  if (job) {
    auto child = job->LookupJobById(koid);
    if (child)
      return out->make(ktl::move(child), rights);
    auto proc = job->LookupProcessById(koid);
    if (proc)
      return out->make(ktl::move(proc), rights);
    return ZX_ERR_NOT_FOUND;
  }

  return ZX_ERR_WRONG_TYPE;
}
