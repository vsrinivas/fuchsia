// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/diagnostics.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <lib/console.h>
#include <lib/ktrace.h>
#include <fbl/auto_lock.h>
#include <object/handles.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <pretty/sizes.h>

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
}

static const char* ObjectTypeToString(mx_obj_type_t type) {
    static_assert(MX_OBJ_TYPE_LAST == 23, "need to update switch below");

    switch (type) {
        case MX_OBJ_TYPE_PROCESS: return "process";
        case MX_OBJ_TYPE_THREAD: return "thread";
        case MX_OBJ_TYPE_VMO: return "vmo";
        case MX_OBJ_TYPE_CHANNEL: return "channel";
        case MX_OBJ_TYPE_EVENT: return "event";
        case MX_OBJ_TYPE_PORT: return "port";
        case MX_OBJ_TYPE_INTERRUPT: return "interrupt";
        case MX_OBJ_TYPE_PCI_DEVICE: return "pci-device";
        case MX_OBJ_TYPE_LOG: return "log";
        case MX_OBJ_TYPE_SOCKET: return "socket";
        case MX_OBJ_TYPE_RESOURCE: return "resource";
        case MX_OBJ_TYPE_EVENT_PAIR: return "event-pair";
        case MX_OBJ_TYPE_JOB: return "job";
        case MX_OBJ_TYPE_VMAR: return "vmar";
        case MX_OBJ_TYPE_FIFO: return "fifo";
        case MX_OBJ_TYPE_GUEST: return "guest";
        case MX_OBJ_TYPE_VCPU: return "vcpu";
        case MX_OBJ_TYPE_TIMER: return "timer";
        default: return "???";
    }
}

// Returns the count of a process's handles. If |handle_type| is non-NULL,
// it should point to |size| elements. For each handle, the corresponding
// mx_obj_type_t-indexed element of |handle_type| is incremented.
static uint32_t BuildHandleStats(const ProcessDispatcher& pd,
                                 uint32_t* handle_type, size_t size) {
    uint32_t total = 0;
    pd.ForEachHandle([&](mx_handle_t handle, mx_rights_t rights,
                         fbl::RefPtr<const Dispatcher> disp) {
        if (handle_type) {
            uint32_t type = static_cast<uint32_t>(disp->get_type());
            if (size > type) {
                ++handle_type[type];
            }
        }
        ++total;
        return MX_OK;
    });
    return total;
}

uint32_t ProcessDispatcher::ThreadCount() const {
    fbl::AutoLock lock(&state_lock_);
    return static_cast<uint32_t>(thread_list_.size_slow());
}

size_t ProcessDispatcher::PageCount() const {
    return aspace_->AllocatedPages();
}

// Counts the process's handles by type and formats them into the provided
// buffer as strings.
static void FormatHandleTypeCount(const ProcessDispatcher& pd,
                                  char *buf, size_t buf_len) {
    uint32_t types[MX_OBJ_TYPE_LAST] = {0};
    uint32_t handle_count = BuildHandleStats(pd, types, sizeof(types));

    snprintf(buf, buf_len, "%4u: %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u",
             handle_count,
             types[MX_OBJ_TYPE_JOB],
             types[MX_OBJ_TYPE_PROCESS],
             types[MX_OBJ_TYPE_THREAD],
             types[MX_OBJ_TYPE_VMO],
             types[MX_OBJ_TYPE_VMAR],
             types[MX_OBJ_TYPE_CHANNEL],
             types[MX_OBJ_TYPE_EVENT] + types[MX_OBJ_TYPE_EVENT_PAIR],
             types[MX_OBJ_TYPE_PORT],
             types[MX_OBJ_TYPE_SOCKET],
             types[MX_OBJ_TYPE_TIMER],
             types[MX_OBJ_TYPE_FIFO]
             );
}

void DumpProcessList() {
    printf("%7s  #h:  #jb #pr #th #vo #vm #ch #ev #po #so #tm #fi [name]\n", "id");

    auto walker = MakeProcessWalker([](ProcessDispatcher* process) {
        char handle_counts[(MX_OBJ_TYPE_LAST * 4) + 1 + /*slop*/ 16];
        FormatHandleTypeCount(*process, handle_counts, sizeof(handle_counts));

        char pname[MX_MAX_NAME_LEN];
        process->get_name(pname);
        printf("%7" PRIu64 "%s [%s]\n",
               process->get_koid(),
               handle_counts,
               pname);
    });
    GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);
}

void DumpJobList() {
    printf("All jobs from least to most important:\n");
    printf("%7s %s\n", "koid", "name");
    JobDispatcher::ForEachJobByImportance([&](JobDispatcher* job) {
        char name[MX_MAX_NAME_LEN];
        job->get_name(name);
        printf("%7" PRIu64 " '%s'\n", job->get_koid(), name);
        return MX_OK;
    });
}

void DumpProcessHandles(mx_koid_t id) {
    auto pd = ProcessDispatcher::LookupProcessById(id);
    if (!pd) {
        printf("process %" PRIu64 " not found!\n", id);
        return;
    }

    printf("process [%" PRIu64 "] handles :\n", id);
    printf("handle       koid : type\n");

    uint32_t total = 0;
    pd->ForEachHandle([&](mx_handle_t handle, mx_rights_t rights,
                          fbl::RefPtr<const Dispatcher> disp) {
        printf("%9x %7" PRIu64 " : %s\n",
            handle, disp->get_koid(), ObjectTypeToString(disp->get_type()));
        ++total;
        return MX_OK;
    });
    printf("total: %u handles\n", total);
}

void ktrace_report_live_processes() {
    auto walker = MakeProcessWalker([](ProcessDispatcher* process) {
        char name[MX_MAX_NAME_LEN];
        process->get_name(name);
        ktrace_name(TAG_PROC_NAME, (uint32_t)process->get_koid(), 0, name);
    });
    GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);
}

// Returns a string representation of VMO-related rights.
static constexpr size_t kRightsStrLen = 8;
static const char* VmoRightsToString(uint32_t rights, char str[kRightsStrLen]) {
    char* c = str;
    *c++ = (rights & MX_RIGHT_READ) ? 'r' : '-';
    *c++ = (rights & MX_RIGHT_WRITE) ? 'w' : '-';
    *c++ = (rights & MX_RIGHT_EXECUTE) ? 'x' : '-';
    *c++ = (rights & MX_RIGHT_MAP) ? 'm' : '-';
    *c++ = (rights & MX_RIGHT_DUPLICATE) ? 'd' : '-';
    *c++ = (rights & MX_RIGHT_TRANSFER) ? 't' : '-';
    *c = '\0';
    return str;
}

// Prints a header for the columns printed by DumpVmObject.
// If |handles| is true, the dumped objects are expected to have handle info.
static void PrintVmoDumpHeader(bool handles) {
    printf(
        "%s koid parent #chld #map #shr    size   alloc name\n",
        handles ? "      handle rights " : "           -      - ");
}

static void DumpVmObject(
    const VmObject& vmo, char format_unit,
    mx_handle_t handle, uint32_t rights, mx_koid_t koid) {

    char handle_str[11];
    if (handle != MX_HANDLE_INVALID) {
        snprintf(handle_str, sizeof(handle_str),
                 "%u", static_cast<uint32_t>(handle));
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
        format_size_fixed(alloc_str, sizeof(alloc_str),
                          vmo.AllocatedPages() * PAGE_SIZE, format_unit);
    } else {
        strlcpy(alloc_str, "phys", sizeof(alloc_str));
    }

    char clone_str[21];
    if (vmo.is_cow_clone()) {
        snprintf(clone_str, sizeof(clone_str),
                 "%" PRIu64, vmo.parent_user_id());
    } else {
        clone_str[0] = '-';
        clone_str[1] = '\0';
    }

    char name[MX_MAX_NAME_LEN];
    vmo.get_name(name, sizeof(name));
    if (name[0] == '\0') {
        name[0] = '-';
        name[1] = '\0';
    }

    printf("  %10s " // handle
           "%6s " // rights
           "%5" PRIu64 " " // koid
           "%6s " // clone parent koid
           "%5" PRIu32 " " // number of children
           "%4" PRIu32 " " // map count
           "%4" PRIu32 " " // share count
           "%7s " // size in bytes
           "%7s " // allocated bytes
           "%s\n", // name
           handle_str,
           rights_str,
           koid,
           clone_str,
           vmo.num_children(),
           vmo.num_mappings(),
           vmo.share_count(),
           size_str,
           alloc_str,
           name);
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
            return MX_OK;
        }
        DumpVmObject(
            vmo,
            format_unit,
            MX_HANDLE_INVALID,
            /* rights */ 0u,
            /* koid */ vmo.user_id());
        // TODO(dbort): Dump the VmAspaces (processes) that map the VMO.
        // TODO(dbort): Dump the processes that hold handles to the VMO.
        //     This will be a lot harder to gather.
        return MX_OK;
    });
    PrintVmoDumpHeader(/* handles */ false);
}

namespace {
// Dumps VMOs under a VmAspace.
class AspaceVmoDumper final : public VmEnumerator {
public:
    AspaceVmoDumper(char format_unit) : format_unit_(format_unit) {}
    bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar,
                     uint depth) final {
        auto vmo = map->vmo();
        DumpVmObject(
            *vmo,
            format_unit_,
            MX_HANDLE_INVALID,
            /* rights */ 0u,
            /* koid */ vmo->user_id());
        return true;
    }
private:
    const char format_unit_;
};
} // namespace

// Dumps all VMOs associated with a process.
static void DumpProcessVmObjects(mx_koid_t id, char format_unit) {
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
    pd->ForEachHandle([&](mx_handle_t handle, mx_rights_t rights,
                          fbl::RefPtr<const Dispatcher> disp) {
        auto vmod = DownCastDispatcher<const VmObjectDispatcher>(&disp);
        if (vmod == nullptr) {
            return MX_OK;
        }
        auto vmo = vmod->vmo();
        DumpVmObject(*vmo, format_unit, handle, rights, vmod->get_koid());

        // TODO: Doesn't handle the case where a process has multiple
        // handles to the same VMO; will double-count all of these totals.
        count++;
        total_size += vmo->size();
        // TODO: Doing this twice (here and in DumpVmObject) is a waste of
        // work, and can get out of sync.
        total_alloc += vmo->AllocatedPages() * PAGE_SIZE;
        return MX_OK;
    });
    char size_str[MAX_FORMAT_SIZE_LEN];
    char alloc_str[MAX_FORMAT_SIZE_LEN];
    printf("  total: %d VMOs, size %s, alloc %s\n",
           count,
           format_size_fixed(size_str, sizeof(size_str),
                             total_size, format_unit),
           format_size_fixed(alloc_str, sizeof(alloc_str),
                             total_alloc, format_unit));

    // Call DumpVmObject() on all VMOs under the process's VmAspace.
    printf("Mapped VMOs:\n");
    PrintVmoDumpHeader(/* handles */ false);
    AspaceVmoDumper avd(format_unit);
    pd->aspace()->EnumerateChildren(&avd);
    PrintVmoDumpHeader(/* handles */ false);
}

void KillProcess(mx_koid_t id) {
    // search the process list and send a kill if found
    auto pd = ProcessDispatcher::LookupProcessById(id);
    if (!pd) {
        printf("process not found!\n");
        return;
    }
    // if found, outside of the lock hit it with kill
    printf("killing process %" PRIu64 "\n", id);
    pd->Kill();
}

namespace {
// Counts memory usage under a VmAspace.
class VmCounter final : public VmEnumerator {
public:
    bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar,
                     uint depth) override {
        usage.mapped_pages += map->size() / PAGE_SIZE;

        size_t committed_pages = map->vmo()->AllocatedPagesInRange(
            map->object_offset(), map->size());
        uint32_t share_count = map->vmo()->share_count();
        if (share_count == 1) {
            usage.private_pages += committed_pages;
        } else {
            usage.shared_pages += committed_pages;
            usage.scaled_shared_bytes +=
                committed_pages * PAGE_SIZE / share_count;
        }
        return true;
    }

    VmAspace::vm_usage_t usage = {};
};
} // namespace

mx_status_t VmAspace::GetMemoryUsage(vm_usage_t* usage) {
    VmCounter vc;
    if (!EnumerateChildren(&vc)) {
        *usage = {};
        return MX_ERR_INTERNAL;
    }
    *usage = vc.usage;
    return MX_OK;
}

namespace {
unsigned int arch_mmu_flags_to_vm_flags(unsigned int arch_mmu_flags) {
    if (arch_mmu_flags & ARCH_MMU_FLAG_INVALID) {
        return 0;
    }
    unsigned int ret = 0;
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_READ) {
        ret |= MX_VM_FLAG_PERM_READ;
    }
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) {
        ret |= MX_VM_FLAG_PERM_WRITE;
    }
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
        ret |= MX_VM_FLAG_PERM_EXECUTE;
    }
    return ret;
}

// Builds a description of an apsace/vmar/mapping hierarchy.
class VmMapBuilder final : public VmEnumerator {
public:
    // NOTE: Code outside of the syscall layer should not typically know about
    // user_ptrs; do not use this pattern as an example.
    VmMapBuilder(user_ptr<mx_info_maps_t> maps, size_t max)
        : maps_(maps), max_(max) {}

    bool OnVmAddressRegion(const VmAddressRegion* vmar, uint depth) override {
        available_++;
        if (nelem_ < max_) {
            mx_info_maps_t entry = {};
            strlcpy(entry.name, vmar->name(), sizeof(entry.name));
            entry.base = vmar->base();
            entry.size = vmar->size();
            entry.depth = depth + 1; // The root aspace is depth 0.
            entry.type = MX_INFO_MAPS_TYPE_VMAR;
            if (maps_.copy_array_to_user(&entry, 1, nelem_) != MX_OK) {
                return false;
            }
            nelem_++;
        }
        return true;
    }

    bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar,
                     uint depth) override {
        available_++;
        if (nelem_ < max_) {
            mx_info_maps_t entry = {};
            auto vmo = map->vmo();
            vmo->get_name(entry.name, sizeof(entry.name));
            entry.base = map->base();
            entry.size = map->size();
            entry.depth = depth + 1; // The root aspace is depth 0.
            entry.type = MX_INFO_MAPS_TYPE_MAPPING;
            mx_info_maps_mapping_t* u = &entry.u.mapping;
            u->mmu_flags =
                arch_mmu_flags_to_vm_flags(map->arch_mmu_flags());
            u->vmo_koid = vmo->user_id();
            u->committed_pages = vmo->AllocatedPagesInRange(
                map->object_offset(), map->size());
            if (maps_.copy_array_to_user(&entry, 1, nelem_) != MX_OK) {
                return false;
            }
            nelem_++;
        }
        return true;
    }

    size_t nelem() const { return nelem_; }
    size_t available() const { return available_; }

private:
    // The caller must write an entry for the root VmAspace at index 0.
    size_t nelem_ = 1;
    size_t available_ = 1;
    user_ptr<mx_info_maps_t> maps_;
    size_t max_;
};
} // namespace

// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
mx_status_t GetVmAspaceMaps(fbl::RefPtr<VmAspace> aspace,
                            user_ptr<mx_info_maps_t> maps, size_t max,
                            size_t* actual, size_t* available) {
    DEBUG_ASSERT(aspace != nullptr);
    *actual = 0;
    *available = 0;
    if (aspace->is_destroyed()) {
        return MX_ERR_BAD_STATE;
    }
    if (max > 0) {
        mx_info_maps_t entry = {};
        strlcpy(entry.name, aspace->name(), sizeof(entry.name));
        entry.base = aspace->base();
        entry.size = aspace->size();
        entry.depth = 0;
        entry.type = MX_INFO_MAPS_TYPE_ASPACE;
        if (maps.copy_array_to_user(&entry, 1, 0) != MX_OK) {
            return MX_ERR_INVALID_ARGS;
        }
    }

    VmMapBuilder b(maps, max);
    if (!aspace->EnumerateChildren(&b)) {
        // VmMapBuilder only returns false
        // when it can't copy to the user pointer.
        return MX_ERR_INVALID_ARGS;
    }
    *actual = max > 0 ? b.nelem() : 0;
    *available = b.available();
    return MX_OK;
}

namespace {
mx_info_vmo_t VmoToInfoEntry(const VmObject* vmo,
                             bool is_handle, mx_rights_t handle_rights) {
    mx_info_vmo_t entry = {};
    entry.koid = vmo->user_id();
    vmo->get_name(entry.name, sizeof(entry.name));
    entry.size_bytes = vmo->size();
    entry.parent_koid = vmo->parent_user_id();
    entry.num_children = vmo->num_children();
    entry.num_mappings = vmo->num_mappings();
    entry.share_count = vmo->share_count();
    entry.flags =
        (vmo->is_paged() ? MX_INFO_VMO_TYPE_PAGED : MX_INFO_VMO_TYPE_PHYSICAL) |
        (vmo->is_cow_clone() ? MX_INFO_VMO_IS_COW_CLONE : 0);
    entry.committed_bytes = vmo->AllocatedPages() * PAGE_SIZE;
    if (is_handle) {
        entry.flags |= MX_INFO_VMO_VIA_HANDLE;
        entry.handle_rights = handle_rights;
    } else {
        entry.flags |= MX_INFO_VMO_VIA_MAPPING;
    }
    return entry;
}

// Builds a list of all VMOs mapped into a VmAspace.
class AspaceVmoEnumerator final : public VmEnumerator {
public:
    // NOTE: Code outside of the syscall layer should not typically know about
    // user_ptrs; do not use this pattern as an example.
    AspaceVmoEnumerator(user_ptr<mx_info_vmo_t> vmos, size_t max)
        : vmos_(vmos), max_(max) {}

    bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar,
                     uint depth) override {
        available_++;
        if (nelem_ < max_) {
            // We're likely to see the same VMO a couple times in a given
            // address space (e.g., somelib.so mapped as r--, r-x), but leave it
            // to userspace to do deduping.
            mx_info_vmo_t entry = VmoToInfoEntry(map->vmo().get(),
                                                 /*is_handle=*/false,
                                                 /*handle_rights=*/0);
            if (vmos_.copy_array_to_user(&entry, 1, nelem_) != MX_OK) {
                return false;
            }
            nelem_++;
        }
        return true;
    }

    size_t nelem() const { return nelem_; }
    size_t available() const { return available_; }

private:
    const user_ptr<mx_info_vmo_t> vmos_;
    const size_t max_;

    size_t nelem_ = 0;
    size_t available_ = 0;
};
} // namespace

// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
mx_status_t GetVmAspaceVmos(fbl::RefPtr<VmAspace> aspace,
                            user_ptr<mx_info_vmo_t> vmos, size_t max,
                            size_t* actual, size_t* available) {
    DEBUG_ASSERT(aspace != nullptr);
    DEBUG_ASSERT(actual != nullptr);
    DEBUG_ASSERT(available != nullptr);
    *actual = 0;
    *available = 0;
    if (aspace->is_destroyed()) {
        return MX_ERR_BAD_STATE;
    }

    AspaceVmoEnumerator ave(vmos, max);
    if (!aspace->EnumerateChildren(&ave)) {
        // AspaceVmoEnumerator only returns false
        // when it can't copy to the user pointer.
        return MX_ERR_INVALID_ARGS;
    }
    *actual = ave.nelem();
    *available = ave.available();
    return MX_OK;
}

// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
mx_status_t GetProcessVmosViaHandles(ProcessDispatcher* process,
                                     user_ptr<mx_info_vmo_t> vmos, size_t max,
                                     size_t* actual_out, size_t* available_out) {
    DEBUG_ASSERT(process != nullptr);
    DEBUG_ASSERT(actual_out != nullptr);
    DEBUG_ASSERT(available_out != nullptr);
    size_t actual = 0;
    size_t available = 0;
    // We may see multiple handles to the same VMO, but leave it to userspace to
    // do deduping.
    mx_status_t s = process->ForEachHandle([&](mx_handle_t handle,
                                               mx_rights_t rights,
                                               fbl::RefPtr<Dispatcher> disp) {
        auto vmod = DownCastDispatcher<VmObjectDispatcher>(&disp);
        if (vmod == nullptr) {
            // This handle isn't a VMO; skip it.
            return MX_OK;
        }
        available++;
        if (actual < max) {
            mx_info_vmo_t entry = VmoToInfoEntry(vmod->vmo().get(),
                                                 /*is_handle=*/true,
                                                 rights);
            if (vmos.copy_array_to_user(&entry, 1, actual) != MX_OK) {
                return MX_ERR_INVALID_ARGS;
            }
            actual++;
        }
        return MX_OK;
    });
    if (s != MX_OK) {
        return s;
    }
    *actual_out = actual;
    *available_out = available;
    return MX_OK;
}

void DumpProcessAddressSpace(mx_koid_t id) {
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
    printf("outstanding handles: %zu\n", internal::OutstandingHandles());
    internal::DumpHandleTableInfo();
}

static size_t mwd_limit = 32 * 256;
static bool mwd_running;

static size_t hwd_limit = 1024;
static bool hwd_running;

static int hwd_thread(void* arg) {
    static size_t previous_handle_count = 0u;

    for (;;) {
        auto handle_count = internal::OutstandingHandles();
        if (handle_count != previous_handle_count) {
            if (handle_count > hwd_limit) {
                printf("HandleWatchdog! %zu handles outstanding (greater than limit %zu)\n",
                       handle_count, hwd_limit);
            } else if (previous_handle_count > hwd_limit) {
                printf("HandleWatchdog! %zu handles outstanding (dropping below limit %zu)\n",
                       handle_count, hwd_limit);
            }
        }

        previous_handle_count = handle_count;

        thread_sleep_relative(LK_SEC(1));
    }
}

void DumpProcessMemoryUsage(const char* prefix, size_t min_pages) {
    auto walker = MakeProcessWalker([&](ProcessDispatcher* process) {
        size_t pages = process->PageCount();
        if (pages >= min_pages) {
            char pname[MX_MAX_NAME_LEN];
            process->get_name(pname);
            printf("%sproc %5" PRIu64 " %4zuM '%s'\n",
                   prefix, process->get_koid(), pages / 256, pname);
        }
    });
    GetRootJobDispatcher()->EnumerateChildren(&walker, /* recurse */ true);
}

static int mwd_thread(void* arg) {
    for (;;) {
        thread_sleep_relative(LK_SEC(1));
        DumpProcessMemoryUsage("MemoryHog! ", mwd_limit);
    }
}

static int cmd_diagnostics(int argc, const cmd_args* argv, uint32_t flags) {
    int rc = 0;

    if (argc < 2) {
        printf("not enough arguments:\n");
    usage:
        printf("%s ps                : list processes\n", argv[0].str);
        printf("%s jobs              : list jobs\n", argv[0].str);
        printf("%s mwd  <mb>         : memory watchdog\n", argv[0].str);
        printf("%s ht   <pid>        : dump process handles\n", argv[0].str);
        printf("%s hwd  <count>      : handle watchdog\n", argv[0].str);
        printf("%s vmos <pid>|all|hidden [-u?]\n", argv[0].str);
        printf("                     : dump process/all/hidden VMOs\n");
        printf("                 -u? : fix all sizes to the named unit\n");
        printf("                       where ? is one of [BkMGTPE]\n");
        printf("%s kill <pid>        : kill process\n", argv[0].str);
        printf("%s asd  <pid>|kernel : dump process/kernel address space\n",
               argv[0].str);
        printf("%s htinfo            : handle table info\n", argv[0].str);
        return -1;
    }

    if (strcmp(argv[1].str, "mwd") == 0) {
        if (argc == 3) {
            mwd_limit = argv[2].u * 256;
        }
        if (!mwd_running) {
            thread_t* t = thread_create("mwd", mwd_thread, nullptr, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
            if (t) {
                mwd_running = true;
                thread_resume(t);
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
            thread_t* t = thread_create("hwd", hwd_thread, nullptr, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
            if (t) {
                hwd_running = true;
                thread_resume(t);
            }
        }
    } else if (strcmp(argv[1].str, "ht") == 0) {
        if (argc < 3)
            goto usage;
        DumpProcessHandles(argv[2].u);
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
    } else {
        printf("unrecognized subcommand '%s'\n", argv[1].str);
        goto usage;
    }
    return rc;
}

STATIC_COMMAND_START
STATIC_COMMAND("mx", "kernel object diagnostics", &cmd_diagnostics)
STATIC_COMMAND_END(mx);
