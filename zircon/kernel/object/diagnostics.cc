// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/diagnostics.h"

#include <inttypes.h>
#include <lib/console.h>
#include <lib/ktrace.h>
#include <stdio.h>
#include <string.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>
#include <ktl/span.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <pretty/sizes.h>
#include <vm/fault.h>

// Machinery to walk over a job tree and run a callback on each process.
template <typename ProcessCallbackType>
class ProcessWalker final : public JobEnumerator {
 public:
  ProcessWalker(ProcessCallbackType cb) : cb_(cb) {}
  ProcessWalker(const ProcessWalker&) = delete;
  ProcessWalker(ProcessWalker&& other) : cb_(other.cb_) {}

 private:
  bool OnProcess(ProcessDispatcher* process) final {
    cb_(process);
    return true;
  }

  const ProcessCallbackType cb_;
};

template <typename ProcessCallbackType>
static ProcessWalker<ProcessCallbackType> MakeProcessWalker(ProcessCallbackType cb) {
  return ProcessWalker<ProcessCallbackType>(cb);
}

// Machinery to walk over a job tree and run a callback on each job.
template <typename JobCallbackType>
class JobWalker final : public JobEnumerator {
 public:
  JobWalker(JobCallbackType cb) : cb_(cb) {}
  JobWalker(const JobWalker&) = delete;
  JobWalker(JobWalker&& other) : cb_(other.cb_) {}

 private:
  bool OnJob(JobDispatcher* job) final {
    cb_(job);
    return true;
  }

  const JobCallbackType cb_;
};

template <typename JobCallbackType>
static JobWalker<JobCallbackType> MakeJobWalker(JobCallbackType cb) {
  return JobWalker<JobCallbackType>(cb);
}

static void DumpProcessListKeyMap() {
  printf("id  : process id number\n");
  printf("#h  : total number of handles\n");
  printf("#jb : number of job handles\n");
  printf("#pr : number of process handles\n");
  printf("#th : number of thread handles\n");
  printf("#vo : number of vmo handles\n");
  printf("#vm : number of virtual memory address region handles\n");
  printf("#ch : number of channel handles\n");
  printf("#ev : number of event and event pair handles\n");
  printf("#po : number of port handles\n");
  printf("#so: number of sockets\n");
  printf("#tm : number of timers\n");
  printf("#fi : number of fifos\n");
  printf("#?? : number of all other handle types\n");
}

static const char* ObjectTypeToString(zx_obj_type_t type) {
  switch (type) {
    case ZX_OBJ_TYPE_PROCESS:
      return "process";
    case ZX_OBJ_TYPE_THREAD:
      return "thread";
    case ZX_OBJ_TYPE_VMO:
      return "vmo";
    case ZX_OBJ_TYPE_CHANNEL:
      return "channel";
    case ZX_OBJ_TYPE_EVENT:
      return "event";
    case ZX_OBJ_TYPE_PORT:
      return "port";
    case ZX_OBJ_TYPE_INTERRUPT:
      return "interrupt";
    case ZX_OBJ_TYPE_PCI_DEVICE:
      return "pci-device";
    case ZX_OBJ_TYPE_LOG:
      return "log";
    case ZX_OBJ_TYPE_SOCKET:
      return "socket";
    case ZX_OBJ_TYPE_RESOURCE:
      return "resource";
    case ZX_OBJ_TYPE_EVENTPAIR:
      return "event-pair";
    case ZX_OBJ_TYPE_JOB:
      return "job";
    case ZX_OBJ_TYPE_VMAR:
      return "vmar";
    case ZX_OBJ_TYPE_FIFO:
      return "fifo";
    case ZX_OBJ_TYPE_GUEST:
      return "guest";
    case ZX_OBJ_TYPE_VCPU:
      return "vcpu";
    case ZX_OBJ_TYPE_TIMER:
      return "timer";
    case ZX_OBJ_TYPE_IOMMU:
      return "iommu";
    case ZX_OBJ_TYPE_BTI:
      return "bti";
    case ZX_OBJ_TYPE_PROFILE:
      return "profile";
    case ZX_OBJ_TYPE_PMT:
      return "pmt";
    case ZX_OBJ_TYPE_SUSPEND_TOKEN:
      return "suspend-token";
    case ZX_OBJ_TYPE_PAGER:
      return "pager";
    case ZX_OBJ_TYPE_EXCEPTION:
      return "exception";
    default:
      return "???";
  }
}

// Returns the count of a process's handles. For each handle, the corresponding
// zx_obj_type_t-indexed element of |handle_types| is incremented.
using HandleTypeCounts = ktl::span<uint32_t, ZX_OBJ_TYPE_UPPER_BOUND>;
static uint32_t BuildHandleStats(const ProcessDispatcher& pd, HandleTypeCounts handle_types) {
  uint32_t total = 0;
  pd.handle_table().ForEachHandle(
      [&](zx_handle_t handle, zx_rights_t rights, const Dispatcher* disp) {
        uint32_t type = static_cast<uint32_t>(disp->get_type());
        ++handle_types[type];
        ++total;
        return ZX_OK;
      });
  return total;
}

// Counts the process's handles by type and formats them into the provided
// buffer as strings.
static void FormatHandleTypeCount(const ProcessDispatcher& pd, char* buf, size_t buf_len) {
  uint32_t types[ZX_OBJ_TYPE_UPPER_BOUND] = {0};
  uint32_t handle_count = BuildHandleStats(pd, types);

  snprintf(buf, buf_len, "%4u: %4u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u", handle_count,
           types[ZX_OBJ_TYPE_JOB], types[ZX_OBJ_TYPE_PROCESS], types[ZX_OBJ_TYPE_THREAD],
           types[ZX_OBJ_TYPE_VMO], types[ZX_OBJ_TYPE_VMAR], types[ZX_OBJ_TYPE_CHANNEL],
           types[ZX_OBJ_TYPE_EVENT] + types[ZX_OBJ_TYPE_EVENTPAIR], types[ZX_OBJ_TYPE_PORT],
           types[ZX_OBJ_TYPE_SOCKET], types[ZX_OBJ_TYPE_TIMER], types[ZX_OBJ_TYPE_FIFO],
           types[ZX_OBJ_TYPE_INTERRUPT] + types[ZX_OBJ_TYPE_PCI_DEVICE] + types[ZX_OBJ_TYPE_LOG] +
               types[ZX_OBJ_TYPE_RESOURCE] + types[ZX_OBJ_TYPE_GUEST] + types[ZX_OBJ_TYPE_VCPU] +
               types[ZX_OBJ_TYPE_IOMMU] + types[ZX_OBJ_TYPE_BTI] + types[ZX_OBJ_TYPE_PROFILE] +
               types[ZX_OBJ_TYPE_PMT] + types[ZX_OBJ_TYPE_SUSPEND_TOKEN] +
               types[ZX_OBJ_TYPE_PAGER] + types[ZX_OBJ_TYPE_EXCEPTION]);
}

void DumpProcessList() {
  printf("%7s  #h:  #jb #pr #th #vo #vm #ch #ev #po #so #tm #fi #?? [name]\n", "id");

  auto walker = MakeProcessWalker([](ProcessDispatcher* process) {
    char handle_counts[(ZX_OBJ_TYPE_UPPER_BOUND * 4) + 1 + /*slop*/ 16];
    FormatHandleTypeCount(*process, handle_counts, sizeof(handle_counts));

    char pname[ZX_MAX_NAME_LEN];
    process->get_name(pname);
    printf("%7" PRIu64 "%s [%s]\n", process->get_koid(), handle_counts, pname);
  });
  GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);
}

void DumpJobList() {
  printf("All jobs:\n");
  printf("%7s %s\n", "koid", "name");
  auto walker = MakeJobWalker([](JobDispatcher* job) {
    char name[ZX_MAX_NAME_LEN];
    job->get_name(name);
    printf("%7" PRIu64 " '%s'\n", job->get_koid(), name);
  });
  GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);
}

void DumpProcessChannels(fbl::RefPtr<ProcessDispatcher> process) {
  char pname[ZX_MAX_NAME_LEN];
  process->get_name(pname);
  printf("%7" PRIu64 " [%s]\n", process->get_koid(), pname);

  process->handle_table().ForEachHandle(
      [&](zx_handle_t handle, zx_rights_t rights, const Dispatcher* disp) {
        if (disp->get_type() == ZX_OBJ_TYPE_CHANNEL) {
          auto chan = DownCastDispatcher<const ChannelDispatcher>(disp);
          uint64_t koid, peer_koid, count, max_count;
          {
            Guard<Mutex> guard{chan->get_lock()};
            koid = chan->get_koid();
            peer_koid = chan->get_related_koid();
            count = chan->get_message_count();
            max_count = chan->get_max_message_count();
          }
          printf("    chan %7" PRIu64 " %7" PRIu64 " count %" PRIu64 " max %" PRIu64 "\n", koid,
                 peer_koid, count, max_count);
        }
        return ZX_OK;
      });
}

void DumpProcessIdChannels(zx_koid_t id) {
  auto pd = ProcessDispatcher::LookupProcessById(id);
  if (!pd) {
    printf("process %" PRIu64 " not found!\n", id);
    return;
  }
  DumpProcessChannels(pd);
}

void DumpAllChannels() {
  auto walker = MakeProcessWalker(
      [](ProcessDispatcher* process) { DumpProcessChannels(fbl::RefPtr(process)); });
  GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);
}

static const char kRightsHeader[] =
    "dup tr r w x map gpr spr enm des spo gpo sig sigp wt ins mj mp mt ap";
static void DumpHandleRightsKeyMap() {
  printf("dup : ZX_RIGHT_DUPLICATE\n");
  printf("tr  : ZX_RIGHT_TRANSFER\n");
  printf("r   : ZX_RIGHT_READ\n");
  printf("w   : ZX_RIGHT_WRITE\n");
  printf("x   : ZX_RIGHT_EXECUTE\n");
  printf("map : ZX_RIGHT_MAP\n");
  printf("gpr : ZX_RIGHT_GET_PROPERTY\n");
  printf("spr : ZX_RIGHT_SET_PROPERTY\n");
  printf("enm : ZX_RIGHT_ENUMERATE\n");
  printf("des : ZX_RIGHT_DESTROY\n");
  printf("spo : ZX_RIGHT_SET_POLICY\n");
  printf("gpo : ZX_RIGHT_GET_POLICY\n");
  printf("sig : ZX_RIGHT_SIGNAL\n");
  printf("sigp: ZX_RIGHT_SIGNAL_PEER\n");
  printf("wt  : ZX_RIGHT_WAIT\n");
  printf("ins : ZX_RIGHT_INSPECT\n");
  printf("mj  : ZX_RIGHT_MANAGE_JOB\n");
  printf("mp  : ZX_RIGHT_MANAGE_PROCESS\n");
  printf("mt  : ZX_RIGHT_MANAGE_THREAD\n");
  printf("ap  : ZX_RIGHT_APPLY_PROFILE\n");
}

static bool HasRights(zx_rights_t rights, zx_rights_t desired) {
  return (rights & desired) == desired;
}

static void FormatHandleRightsMask(zx_rights_t rights, char* buf, size_t buf_len) {
  snprintf(buf, buf_len,
           "%3d %2d %1d %1d %1d %3d %3d %3d %3d %3d %3d %3d %3d %4d %2d %3d %2d %2d %2d %2d",
           HasRights(rights, ZX_RIGHT_DUPLICATE), HasRights(rights, ZX_RIGHT_TRANSFER),
           HasRights(rights, ZX_RIGHT_READ), HasRights(rights, ZX_RIGHT_WRITE),
           HasRights(rights, ZX_RIGHT_EXECUTE), HasRights(rights, ZX_RIGHT_MAP),
           HasRights(rights, ZX_RIGHT_GET_PROPERTY), HasRights(rights, ZX_RIGHT_SET_PROPERTY),
           HasRights(rights, ZX_RIGHT_ENUMERATE), HasRights(rights, ZX_RIGHT_DESTROY),
           HasRights(rights, ZX_RIGHT_SET_POLICY), HasRights(rights, ZX_RIGHT_GET_POLICY),
           HasRights(rights, ZX_RIGHT_SIGNAL), HasRights(rights, ZX_RIGHT_SIGNAL_PEER),
           HasRights(rights, ZX_RIGHT_WAIT), HasRights(rights, ZX_RIGHT_INSPECT),
           HasRights(rights, ZX_RIGHT_MANAGE_JOB), HasRights(rights, ZX_RIGHT_MANAGE_PROCESS),
           HasRights(rights, ZX_RIGHT_MANAGE_THREAD), HasRights(rights, ZX_RIGHT_APPLY_PROFILE));
}

void DumpProcessHandles(zx_koid_t id) {
  auto pd = ProcessDispatcher::LookupProcessById(id);
  if (!pd) {
    printf("process %" PRIu64 " not found!\n", id);
    return;
  }

  char pname[ZX_MAX_NAME_LEN];
  pd->get_name(pname);
  printf("process %" PRIu64 " ('%s') handles:\n", id, pname);
  printf("%7s %10s %10s: {%s} [type]\n", "koid", "handle", "rights", kRightsHeader);

  uint32_t total = 0;
  pd->handle_table().ForEachHandle(
      [&](zx_handle_t handle, zx_rights_t rights, const Dispatcher* disp) {
        char rights_mask[sizeof(kRightsHeader)];
        FormatHandleRightsMask(rights, rights_mask, sizeof(rights_mask));
        printf("%7" PRIu64 " %#10x %#10x: {%s} [%s]\n", disp->get_koid(), handle, rights,
               rights_mask, ObjectTypeToString(disp->get_type()));
        ++total;
        return ZX_OK;
      });
  printf("total: %u handles\n", total);
}

void DumpHandlesForKoid(zx_koid_t id) {
  if (id < ZX_KOID_FIRST) {
    printf("invalid koid, non-reserved koids start at %" PRIu64 "\n", ZX_KOID_FIRST);
    return;
  }

  uint32_t total_proc = 0;
  uint32_t total_handles = 0;
  auto walker = MakeProcessWalker([&](ProcessDispatcher* process) {
    bool found_handle = false;
    process->handle_table().ForEachHandle([&](zx_handle_t handle, zx_rights_t rights,
                                              const Dispatcher* disp) {
      if (disp->get_koid() != id) {
        return ZX_OK;
      }

      if (total_handles == 0) {
        printf("handles for koid %" PRIu64 " (%s):\n", id, ObjectTypeToString(disp->get_type()));
        printf("%7s %10s: {%s} [name]\n", "pid", "rights", kRightsHeader);
      }

      char pname[ZX_MAX_NAME_LEN];
      char rights_mask[sizeof(kRightsHeader)];
      process->get_name(pname);
      FormatHandleRightsMask(rights, rights_mask, sizeof(rights_mask));
      printf("%7" PRIu64 " %#10x: {%s} [%s]\n", process->get_koid(), rights, rights_mask, pname);

      ++total_handles;
      found_handle = true;
      return ZX_OK;
    });
    total_proc += found_handle;
  });
  GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);

  if (total_handles > 0) {
    printf("total: %u handles in %u processes\n", total_handles, total_proc);
  } else {
    printf("no handles found for koid %" PRIu64 "\n", id);
  }
}

void ktrace_report_live_processes() {
  auto walker = MakeProcessWalker([](ProcessDispatcher* process) {
    char name[ZX_MAX_NAME_LEN];
    process->get_name(name);
    ktrace_name(TAG_PROC_NAME, (uint32_t)process->get_koid(), 0, name);
  });
  GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);
}

// Returns a string representation of VMO-related rights.
static constexpr size_t kRightsStrLen = 8;
static const char* VmoRightsToString(uint32_t rights, char str[kRightsStrLen]) {
  char* c = str;
  *c++ = (rights & ZX_RIGHT_READ) ? 'r' : '-';
  *c++ = (rights & ZX_RIGHT_WRITE) ? 'w' : '-';
  *c++ = (rights & ZX_RIGHT_EXECUTE) ? 'x' : '-';
  *c++ = (rights & ZX_RIGHT_MAP) ? 'm' : '-';
  *c++ = (rights & ZX_RIGHT_DUPLICATE) ? 'd' : '-';
  *c++ = (rights & ZX_RIGHT_TRANSFER) ? 't' : '-';
  *c = '\0';
  return str;
}

// Prints a header for the columns printed by DumpVmObject.
// If |handles| is true, the dumped objects are expected to have handle info.
static void PrintVmoDumpHeader(bool handles) {
  printf("%s koid obj                parent #chld #map #shr    size   alloc name\n",
         handles ? "      handle rights " : "           -      - ");
}

static void DumpVmObject(const VmObject& vmo, char format_unit, zx_handle_t handle, uint32_t rights,
                         zx_koid_t koid) {
  char handle_str[11];
  if (handle != ZX_HANDLE_INVALID) {
    snprintf(handle_str, sizeof(handle_str), "%u", static_cast<uint32_t>(handle));
  } else {
    handle_str[0] = '-';
    handle_str[1] = '\0';
  }

  char rights_str[kRightsStrLen];
  if (rights != 0) {
    VmoRightsToString(rights, rights_str);
  } else {
    rights_str[0] = '-';
    rights_str[1] = '\0';
  }

  char size_str[MAX_FORMAT_SIZE_LEN];
  format_size_fixed(size_str, sizeof(size_str), vmo.size(), format_unit);

  char alloc_str[MAX_FORMAT_SIZE_LEN];
  if (vmo.is_paged()) {
    format_size_fixed(alloc_str, sizeof(alloc_str), vmo.AttributedPages() * PAGE_SIZE, format_unit);
  } else {
    strlcpy(alloc_str, "phys", sizeof(alloc_str));
  }

  char child_str[21];
  if (vmo.child_type() != VmObject::kNotChild) {
    snprintf(child_str, sizeof(child_str), "%" PRIu64, vmo.parent_user_id());
  } else {
    child_str[0] = '-';
    child_str[1] = '\0';
  }

  char name[ZX_MAX_NAME_LEN];
  vmo.get_name(name, sizeof(name));
  if (name[0] == '\0') {
    name[0] = '-';
    name[1] = '\0';
  }

  printf(
      "  %10s "  // handle
      "%6s "     // rights
      "%5" PRIu64
      " "     // koid
      "%p "   // vm_object
      "%6s "  // child parent koid
      "%5" PRIu32
      " "  // number of children
      "%4" PRIu32
      " "  // map count
      "%4" PRIu32
      " "      // share count
      "%7s "   // size in bytes
      "%7s "   // allocated bytes
      "%s\n",  // name
      handle_str, rights_str, koid, &vmo, child_str, vmo.num_children(), vmo.num_mappings(),
      vmo.share_count(), size_str, alloc_str, name);
}

// If |hidden_only| is set, will only dump VMOs that are not mapped
// into any process:
// - VMOs that userspace has handles to but does not map
// - VMOs that are mapped only into kernel space
// - Kernel-only, unmapped VMOs that have no handles
static void DumpAllVmObjects(bool hidden_only, char format_unit) {
  if (hidden_only) {
    printf("\"Hidden\" VMOs, oldest to newest:\n");
  } else {
    printf("All VMOs, oldest to newest:\n");
  }
  PrintVmoDumpHeader(/* handles */ false);
  VmObject::ForEach([=](const VmObject& vmo) {
    if (hidden_only && vmo.IsMappedByUser()) {
      return ZX_OK;
    }
    DumpVmObject(vmo, format_unit, ZX_HANDLE_INVALID,
                 /* rights */ 0u,
                 /* koid */ vmo.user_id());
    // TODO(dbort): Dump the VmAspaces (processes) that map the VMO.
    // TODO(dbort): Dump the processes that hold handles to the VMO.
    //     This will be a lot harder to gather.
    return ZX_OK;
  });
  PrintVmoDumpHeader(/* handles */ false);
}

namespace {
// Dumps VMOs under a VmAspace.
class AspaceVmoDumper final : public VmEnumerator {
 public:
  AspaceVmoDumper(char format_unit) : format_unit_(format_unit) {}
  bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar, uint depth) final {
    auto vmo = map->vmo_locked();
    DumpVmObject(*vmo, format_unit_, ZX_HANDLE_INVALID,
                 /* rights */ 0u,
                 /* koid */ vmo->user_id());
    return true;
  }

 private:
  const char format_unit_;
};
}  // namespace

// Dumps all VMOs associated with a process.
static void DumpProcessVmObjects(zx_koid_t id, char format_unit) {
  auto pd = ProcessDispatcher::LookupProcessById(id);
  if (!pd) {
    printf("process not found!\n");
    return;
  }

  printf("process [%" PRIu64 "]:\n", id);
  printf("Handles to VMOs:\n");
  PrintVmoDumpHeader(/* handles */ true);
  int count = 0;
  uint64_t total_size = 0;
  uint64_t total_alloc = 0;
  pd->handle_table().ForEachHandle(
      [&](zx_handle_t handle, zx_rights_t rights, const Dispatcher* disp) {
        auto vmod = DownCastDispatcher<const VmObjectDispatcher>(disp);
        if (vmod == nullptr) {
          return ZX_OK;
        }
        auto vmo = vmod->vmo();
        DumpVmObject(*vmo, format_unit, handle, rights, vmod->get_koid());

        // TODO: Doesn't handle the case where a process has multiple
        // handles to the same VMO; will double-count all of these totals.
        count++;
        total_size += vmo->size();
        // TODO: Doing this twice (here and in DumpVmObject) is a waste of
        // work, and can get out of sync.
        total_alloc += vmo->AttributedPages() * PAGE_SIZE;
        return ZX_OK;
      });
  char size_str[MAX_FORMAT_SIZE_LEN];
  char alloc_str[MAX_FORMAT_SIZE_LEN];
  printf("  total: %d VMOs, size %s, alloc %s\n", count,
         format_size_fixed(size_str, sizeof(size_str), total_size, format_unit),
         format_size_fixed(alloc_str, sizeof(alloc_str), total_alloc, format_unit));

  // Call DumpVmObject() on all VMOs under the process's VmAspace.
  printf("Mapped VMOs:\n");
  PrintVmoDumpHeader(/* handles */ false);
  AspaceVmoDumper avd(format_unit);
  pd->aspace()->EnumerateChildren(&avd);
  PrintVmoDumpHeader(/* handles */ false);
}

void KillProcess(zx_koid_t id) {
  // search the process list and send a kill if found
  auto pd = ProcessDispatcher::LookupProcessById(id);
  if (!pd) {
    printf("process not found!\n");
    return;
  }
  // if found, outside of the lock hit it with kill
  printf("killing process %" PRIu64 "\n", id);
  pd->Kill(ZX_TASK_RETCODE_SYSCALL_KILL);
}

namespace {
// Counts memory usage under a VmAspace.
class VmCounter final : public VmEnumerator {
 public:
  bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar, uint depth) override {
    usage.mapped_pages += map->size() / PAGE_SIZE;

    auto vmo = map->vmo_locked();
    size_t committed_pages = vmo->AttributedPagesInRange(map->object_offset(), map->size());
    uint32_t share_count = vmo->share_count();
    if (share_count == 1) {
      usage.private_pages += committed_pages;
    } else {
      usage.shared_pages += committed_pages;
      usage.scaled_shared_bytes += committed_pages * PAGE_SIZE / share_count;
    }
    return true;
  }

  VmAspace::vm_usage_t usage = {};
};
}  // namespace

zx_status_t VmAspace::GetMemoryUsage(vm_usage_t* usage) {
  VmCounter vc;
  if (!EnumerateChildren(&vc)) {
    *usage = {};
    return ZX_ERR_INTERNAL;
  }
  *usage = vc.usage;
  return ZX_OK;
}

namespace {
unsigned int arch_mmu_flags_to_vm_flags(unsigned int arch_mmu_flags) {
  if (arch_mmu_flags & ARCH_MMU_FLAG_INVALID) {
    return 0;
  }
  unsigned int ret = 0;
  if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_READ) {
    ret |= ZX_VM_PERM_READ;
  }
  if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) {
    ret |= ZX_VM_PERM_WRITE;
  }
  if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
    ret |= ZX_VM_PERM_EXECUTE;
  }
  return ret;
}

// This provides a generic way to perform VmAspace::EnumerateChildren in scenarios where the
// enumeration may need to be retried due to page faults for user copies needing to be handled.
// Mostly it serves to reduce the duplication in logic between the VmMapBuilder and the
// AspaceVmoEnumerator and so the template options exist to handle precisely those two cases.
// IMPL is the object type that the Make* methods will be called on if the respective Enumerate*
// options are true.
template <typename ENTRY, typename IMPL, bool EnumerateVmar, bool EnumerateMapping,
          size_t FirstEntry>
class RestartableVmEnumerator {
 public:
  // max is the total number of elements that entries can support, with FirstEntry being the first
  // of these entries that this enumerator will store to. This means we can write at most
  // max-FirstEntry entries.
  RestartableVmEnumerator(size_t max) : max_(max) {}

  zx_status_t Enumerate(VmAspace* target) {
    nelem_ = FirstEntry;
    available_ = FirstEntry;
    start_ = 0;
    faults_ = 0;
    visited_ = 0;

    // EnumerateChildren only fails if copying to the user hit a fault. We redo the copy outside
    // of the enumeration so that we're not holding the aspace lock. If it still fails then we
    // consider it an error, otherwise we restart the enumeration skipping any entries with a
    // virtual address in the segment we already processed. A segment is represented by an address
    // and a depth pair, as vmars/mappings can exist at the same base address due to them being
    // hierarchical, but they will have a higher depth.
    Enumerator enumerator{this};
    while (!target->EnumerateChildren(&enumerator)) {
      DEBUG_ASSERT(nelem_ < max_);
      zx_status_t result = WriteEntry(entry_, nelem_);
      if (result != ZX_OK) {
        return result;
      }
      nelem_++;
    }

    // This aims to ensure that the logic of skipping already processed segments does not cause us
    // to miss any segments. Guards against the VmAspace failing to correctly enumerate in depth
    // first order.
    DEBUG_ASSERT(faults_ > 0 || visited_ + FirstEntry == available_);

    return ZX_OK;
  }

  size_t nelem() const { return nelem_; }
  size_t available() const { return available_; }

 protected:
  virtual zx_status_t WriteEntry(const ENTRY& entry, size_t offset) = 0;
  virtual UserCopyCaptureFaultsResult WriteEntryCaptureFaults(const ENTRY& entry,
                                                              size_t offset) = 0;

 private:
  class Enumerator : public VmEnumerator {
   public:
    Enumerator(RestartableVmEnumerator* parent) : parent_(parent) {}

    bool OnVmAddressRegion([[maybe_unused]] const VmAddressRegion* vmar,
                           [[maybe_unused]] uint depth) override {
      if constexpr (EnumerateVmar) {
        return parent_->DoEntry(
            [vmar, depth, this] { IMPL::MakeVmarEntry(vmar, depth, &parent_->entry_); },
            vmar->base(), depth);
      }
      return true;
    }

    bool OnVmMapping([[maybe_unused]] const VmMapping* map,
                     [[maybe_unused]] const VmAddressRegion* vmar,
                     [[maybe_unused]] uint depth) override {
      if constexpr (EnumerateMapping) {
        return parent_->DoEntry(
            [map, vmar, depth, this] {
              IMPL::MakeMappingEntry(map, vmar, depth, &parent_->entry_);
            },
            map->base(), depth);
      }
      return true;
    }

   private:
    RestartableVmEnumerator* parent_;
  };
  friend Enumerator;

  // This helper is templated to allow the maximum code sharing between the two On* callbacks.
  template <typename F>
  bool DoEntry(F&& make_entry, zx_vaddr_t base, uint depth) {
    visited_++;
    // Skip anything that is at an earlier address or depth to prevent us double processing any
    // segments.
    if (base < start_ || (base == start_ && depth < start_depth_)) {
      return true;
    }
    // Whatever happens we never want to process this again. We set this *always*, and not just on
    // faults so that the logic of skipping above is consistently applied helping catch any bugs in
    // changes to enumeration order.
    start_ = base;
    start_depth_ = depth + 1;

    available_++;
    if (nelem_ >= max_) {
      return true;
    }
    ktl::forward<F>(make_entry)();

    UserCopyCaptureFaultsResult res = WriteEntryCaptureFaults(entry_, nelem_);
    if (res.status != ZX_OK) {
      // This entry will get written out by the main loop, so return false to break all the way out.
      faults_++;
      return false;
    }

    nelem_++;
    return true;
  }

  const size_t max_;

  // Use a single ENTRY stashed here and pass it by reference anywhere as this can be a large
  // structure and we want to avoid multiple stack allocations from occurring.
  ENTRY entry_{};
  static_assert(sizeof(ENTRY) < 512);
  size_t nelem_ = 0;
  size_t available_ = 0;

  zx_vaddr_t start_ = 0;
  uint start_depth_ = 0;

  // Count some statistics so we can do some lightweight sanity checking that we correctly process
  // everything.
  size_t faults_ = 0;
  size_t visited_ = 0;
};

// Builds a description of an apsace/vmar/mapping hierarchy. Entries start at 1 as the user must
// write an entry for the root VmAspace at index 0.
class VmMapBuilder final
    : public RestartableVmEnumerator<zx_info_maps_t, VmMapBuilder, true, true, 1> {
 public:
  // NOTE: Code outside of the syscall layer should not typically know about
  // user_ptrs; do not use this pattern as an example.
  VmMapBuilder(user_out_ptr<zx_info_maps_t> maps, size_t max)
      : RestartableVmEnumerator(max), entries_(maps) {}

  static void MakeVmarEntry(const VmAddressRegion* vmar, uint depth, zx_info_maps_t* entry) {
    *entry = {};
    strlcpy(entry->name, vmar->name(), sizeof(entry->name));
    entry->base = vmar->base();
    entry->size = vmar->size();
    entry->depth = depth + 1;  // The root aspace is depth 0.
    entry->type = ZX_INFO_MAPS_TYPE_VMAR;
  }

  static void MakeMappingEntry(const VmMapping* map, const VmAddressRegion* vmar, uint depth,
                               zx_info_maps_t* entry) {
    *entry = {};
    auto vmo = map->vmo_locked();
    vmo->get_name(entry->name, sizeof(entry->name));
    entry->base = map->base();
    entry->size = map->size();
    entry->depth = depth + 1;  // The root aspace is depth 0.
    entry->type = ZX_INFO_MAPS_TYPE_MAPPING;
    zx_info_maps_mapping_t* u = &entry->u.mapping;
    u->mmu_flags = arch_mmu_flags_to_vm_flags(map->arch_mmu_flags());
    u->vmo_koid = vmo->user_id();
    u->committed_pages = vmo->AttributedPagesInRange(map->object_offset(), map->size());
    u->vmo_offset = map->object_offset();
  }

 protected:
  zx_status_t WriteEntry(const zx_info_maps_t& entry, size_t offset) {
    return entries_.element_offset(offset).copy_to_user(entry);
  }
  UserCopyCaptureFaultsResult WriteEntryCaptureFaults(const zx_info_maps_t& entry, size_t offset) {
    return entries_.element_offset(offset).copy_to_user_capture_faults(entry);
  }
  const user_out_ptr<zx_info_maps_t> entries_;
};
}  // namespace

// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
zx_status_t GetVmAspaceMaps(VmAspace* current_aspace, fbl::RefPtr<VmAspace> target_aspace,
                            user_out_ptr<zx_info_maps_t> maps, size_t max, size_t* actual,
                            size_t* available) {
  DEBUG_ASSERT(target_aspace != nullptr);
  *actual = 0;
  *available = 0;
  if (target_aspace->is_destroyed()) {
    return ZX_ERR_BAD_STATE;
  }
  if (max > 0) {
    zx_info_maps_t entry = {};
    strlcpy(entry.name, target_aspace->name(), sizeof(entry.name));
    entry.base = target_aspace->base();
    entry.size = target_aspace->size();
    entry.depth = 0;
    entry.type = ZX_INFO_MAPS_TYPE_ASPACE;
    if (maps.copy_array_to_user(&entry, 1, 0) != ZX_OK) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  VmMapBuilder b(maps, max);

  zx_status_t status = b.Enumerate(target_aspace.get());
  if (status != ZX_OK) {
    return status;
  }

  *actual = max > 0 ? b.nelem() : 0;
  *available = b.available();
  return ZX_OK;
}

namespace {
// Builds a list of all VMOs mapped into a VmAspace.
class AspaceVmoEnumerator final
    : public RestartableVmEnumerator<zx_info_vmo_t, AspaceVmoEnumerator, false, true, 0> {
 public:
  // NOTE: Code outside of the syscall layer should not typically know about
  // user_ptrs; do not use this pattern as an example.
  AspaceVmoEnumerator(VmoInfoWriter& vmos, size_t max)
      : RestartableVmEnumerator(max), vmos_(vmos) {}

  static void MakeMappingEntry(const VmMapping* map, const VmAddressRegion* vmar, uint depth,
                               zx_info_vmo_t* entry) {
    // We're likely to see the same VMO a couple times in a given
    // address space (e.g., somelib.so mapped as r--, r-x), but leave it
    // to userspace to do deduping.
    *entry = VmoToInfoEntry(map->vmo_locked().get(),
                            /*is_handle=*/false,
                            /*handle_rights=*/0);
  }

 protected:
  zx_status_t WriteEntry(const zx_info_vmo_t& entry, size_t offset) {
    return vmos_.Write(entry, offset);
  }
  UserCopyCaptureFaultsResult WriteEntryCaptureFaults(const zx_info_vmo_t& entry, size_t offset) {
    return vmos_.WriteCaptureFaults(entry, offset);
  }
  VmoInfoWriter& vmos_;
};
}  // namespace

// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
zx_status_t GetVmAspaceVmos(VmAspace* current_aspace, fbl::RefPtr<VmAspace> target_aspace,
                            VmoInfoWriter& vmos, size_t max, size_t* actual, size_t* available) {
  DEBUG_ASSERT(target_aspace != nullptr);
  DEBUG_ASSERT(actual != nullptr);
  DEBUG_ASSERT(available != nullptr);
  *actual = 0;
  *available = 0;
  if (target_aspace->is_destroyed()) {
    return ZX_ERR_BAD_STATE;
  }

  AspaceVmoEnumerator ave(vmos, max);

  zx_status_t status = ave.Enumerate(target_aspace.get());
  if (status != ZX_OK) {
    return status;
  }

  *actual = ave.nelem();
  *available = ave.available();
  return ZX_OK;
}

// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
zx_status_t GetProcessVmos(ProcessDispatcher* process, VmoInfoWriter& vmos, size_t max,
                           size_t* actual_out, size_t* available_out) {
  DEBUG_ASSERT(process != nullptr);
  DEBUG_ASSERT(actual_out != nullptr);
  DEBUG_ASSERT(available_out != nullptr);
  size_t actual = 0;
  size_t available = 0;
  // We may see multiple handles to the same VMO, but leave it to userspace to
  // do deduping.
  zx_status_t s = process->handle_table().ForEachHandleBatched(
      [&](zx_handle_t handle, zx_rights_t rights, const Dispatcher* disp) {
        auto vmod = DownCastDispatcher<const VmObjectDispatcher>(disp);
        if (vmod == nullptr) {
          // This handle isn't a VMO; skip it.
          return ZX_OK;
        }
        available++;
        if (actual < max) {
          zx_info_vmo_t entry = VmoToInfoEntry(vmod->vmo().get(),
                                               /*is_handle=*/true, rights);
          if (vmos.Write(entry, actual) != ZX_OK) {
            return ZX_ERR_INVALID_ARGS;
          }
          actual++;
        }
        return ZX_OK;
      });
  if (s != ZX_OK) {
    return s;
  }
  *actual_out = actual;
  *available_out = available;
  return ZX_OK;
}

void DumpProcessAddressSpace(zx_koid_t id) {
  auto pd = ProcessDispatcher::LookupProcessById(id);
  if (!pd) {
    printf("process %" PRIu64 " not found!\n", id);
    return;
  }

  pd->aspace()->Dump(true);
}

// Dumps an address space based on the arg.
static void DumpAddressSpace(const cmd_args* arg) {
  if (strncmp(arg->str, "kernel", strlen(arg->str)) == 0) {
    // The arg is a prefix of "kernel".
    VmAspace::kernel_aspace()->Dump(true);
  } else {
    DumpProcessAddressSpace(arg->u);
  }
}

static void DumpHandleTable() {
  printf("outstanding handles: %zu\n", Handle::diagnostics::OutstandingHandles());
  Handle::diagnostics::DumpTableInfo();
}

static size_t mwd_limit = 32 * 256;
static bool mwd_running;

static size_t hwd_limit = 1024;
static bool hwd_running;

static int hwd_thread(void* arg) {
  static size_t previous_handle_count = 0u;

  for (;;) {
    auto handle_count = Handle::diagnostics::OutstandingHandles();
    if (handle_count != previous_handle_count) {
      if (handle_count > hwd_limit) {
        printf("HandleWatchdog! %zu handles outstanding (greater than limit %zu)\n", handle_count,
               hwd_limit);
      } else if (previous_handle_count > hwd_limit) {
        printf("HandleWatchdog! %zu handles outstanding (dropping below limit %zu)\n", handle_count,
               hwd_limit);
      }
    }

    previous_handle_count = handle_count;

    Thread::Current::SleepRelative(ZX_SEC(1));
  }
}

void DumpProcessMemoryUsage(const char* prefix, size_t min_pages) {
  auto walker = MakeProcessWalker([&](ProcessDispatcher* process) {
    size_t pages = process->PageCount();
    if (pages >= min_pages) {
      char pname[ZX_MAX_NAME_LEN];
      process->get_name(pname);
      printf("%sproc %5" PRIu64 " %4zuM '%s'\n", prefix, process->get_koid(), pages / 256, pname);
    }
  });
  GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);
}

static int mwd_thread(void* arg) {
  for (;;) {
    Thread::Current::SleepRelative(ZX_SEC(1));
    DumpProcessMemoryUsage("MemoryHog! ", mwd_limit);
  }
}

static int cmd_diagnostics(int argc, const cmd_args* argv, uint32_t flags) {
  int rc = 0;

  if (argc < 2) {
    printf("not enough arguments:\n");
  usage:
    printf("%s ps                : list processes\n", argv[0].str);
    printf("%s ps help           : print header label descriptions for 'ps'\n", argv[0].str);
    printf("%s jobs              : list jobs\n", argv[0].str);
    printf("%s mwd  <mb>         : memory watchdog\n", argv[0].str);
    printf("%s ht   <pid>        : dump process handles\n", argv[0].str);
    printf("%s ch   <pid>        : dump process channels for pid or for all processes\n",
           argv[0].str);
    printf("%s hwd  <count>      : handle watchdog\n", argv[0].str);
    printf("%s vmos <pid>|all|hidden [-u?]\n", argv[0].str);
    printf("                     : dump process/all/hidden VMOs\n");
    printf("                 -u? : fix all sizes to the named unit\n");
    printf("                       where ? is one of [BkMGTPE]\n");
    printf("%s kill <pid>        : kill process\n", argv[0].str);
    printf("%s asd  <pid>|kernel : dump process/kernel address space\n", argv[0].str);
    printf("%s htinfo            : handle table info\n", argv[0].str);
    printf("%s koid <koid>       : list all handles for a koid\n", argv[0].str);
    printf("%s koid help         : print header label descriptions for 'koid'\n", argv[0].str);
    return -1;
  }

  if (strcmp(argv[1].str, "mwd") == 0) {
    if (argc == 3) {
      mwd_limit = argv[2].u * 256;
    }
    if (!mwd_running) {
      Thread* t = Thread::Create("mwd", mwd_thread, nullptr, DEFAULT_PRIORITY);
      if (t) {
        mwd_running = true;
        t->Resume();
      }
    }
  } else if (strcmp(argv[1].str, "ps") == 0) {
    if ((argc == 3) && (strcmp(argv[2].str, "help") == 0)) {
      DumpProcessListKeyMap();
    } else {
      DumpProcessList();
    }
  } else if (strcmp(argv[1].str, "jobs") == 0) {
    DumpJobList();
  } else if (strcmp(argv[1].str, "hwd") == 0) {
    if (argc == 3) {
      hwd_limit = argv[2].u;
    }
    if (!hwd_running) {
      Thread* t = Thread::Create("hwd", hwd_thread, nullptr, DEFAULT_PRIORITY);
      if (t) {
        hwd_running = true;
        t->Resume();
      }
    }
  } else if (strcmp(argv[1].str, "ht") == 0) {
    if (argc < 3)
      goto usage;
    DumpProcessHandles(argv[2].u);
  } else if (strcmp(argv[1].str, "ch") == 0) {
    if (argc == 3) {
      DumpProcessIdChannels(argv[2].u);
    } else {
      DumpAllChannels();
    }
  } else if (strcmp(argv[1].str, "vmos") == 0) {
    if (argc < 3)
      goto usage;
    char format_unit = 0;
    if (argc >= 4) {
      if (!strncmp(argv[3].str, "-u", sizeof("-u") - 1)) {
        format_unit = argv[3].str[sizeof("-u") - 1];
      } else {
        printf("dunno '%s'\n", argv[3].str);
        goto usage;
      }
    }
    if (strcmp(argv[2].str, "all") == 0) {
      DumpAllVmObjects(/*hidden_only=*/false, format_unit);
    } else if (strcmp(argv[2].str, "hidden") == 0) {
      DumpAllVmObjects(/*hidden_only=*/true, format_unit);
    } else {
      DumpProcessVmObjects(argv[2].u, format_unit);
    }
  } else if (strcmp(argv[1].str, "kill") == 0) {
    if (argc < 3)
      goto usage;
    KillProcess(argv[2].u);
  } else if (strcmp(argv[1].str, "asd") == 0) {
    if (argc < 3)
      goto usage;
    DumpAddressSpace(&argv[2]);
  } else if (strcmp(argv[1].str, "htinfo") == 0) {
    if (argc != 2)
      goto usage;
    DumpHandleTable();
  } else if (strcmp(argv[1].str, "koid") == 0) {
    if (argc < 3)
      goto usage;

    if (strcmp(argv[2].str, "help") == 0) {
      DumpHandleRightsKeyMap();
    } else {
      DumpHandlesForKoid(argv[2].u);
    }
  } else {
    printf("unrecognized subcommand '%s'\n", argv[1].str);
    goto usage;
  }
  return rc;
}

STATIC_COMMAND_START
STATIC_COMMAND("zx", "kernel object diagnostics", &cmd_diagnostics)
STATIC_COMMAND_END(zx)
