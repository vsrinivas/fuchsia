// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <err.h>
#include <platform.h>
#include <string.h>

#include <arch/ops.h>
#include <arch/user_copy.h>
#include <fbl/alloc_checker.h>
#include <hypervisor/ktrace.h>
#include <kernel/cmdline.h>
#include <lib/ktrace.h>
#include <lib/ktrace/string_ref.h>
#include <lk/init.h>
#include <object/thread_dispatcher.h>
#include <vm/vm_aspace.h>
#include <zircon/thread_annotations.h>

#define ktrace_timestamp() current_ticks();
#define ktrace_ticks_per_ms() (ticks_per_second() / 1000)

// Generated struct that has the syscall index and name.
static struct ktrace_syscall_info {
    uint32_t id;
    uint32_t nargs;
    const char* name;
} kt_syscall_info[] = {
#include <zircon/syscall-ktrace-info.inc>
    {0, 0, nullptr}};

void ktrace_report_syscalls(ktrace_syscall_info* call) {
    size_t ix = 0;
    while (call[ix].name) {
        ktrace_name_etc(TAG_SYSCALL_NAME, call[ix].id, 0, call[ix].name, true);
        ++ix;
    }
}

static fbl::Mutex probe_list_lock;

static StringRef* ktrace_find_probe(const char* name) TA_REQ(probe_list_lock) {
    for (StringRef* ref = StringRef::head(); ref != nullptr; ref = ref->next) {
        if (!strcmp(name, ref->string)) {
            return ref;
        }
    }
    return nullptr;
}

static void ktrace_add_probe(StringRef* string_ref) TA_REQ(probe_list_lock) {
    // Register and emit the string ref.
    string_ref->GetId();
}

static void ktrace_report_probes(void) {
    fbl::AutoLock lock(&probe_list_lock);
    for (StringRef* ref = StringRef::head(); ref != nullptr; ref = ref->next) {
        ktrace_name_etc(TAG_PROBE_NAME, ref->id, 0, ref->string, true);
    }
}

typedef struct ktrace_state {
    // where the next record will be written
    int offset;

    // mask of groups we allow, 0 == tracing disabled
    int grpmask;

    // total size of the trace buffer
    uint32_t bufsize;

    // offset where tracing was stopped, 0 if tracing active
    uint32_t marker;

    // raw trace buffer
    uint8_t* buffer;
} ktrace_state_t;

static ktrace_state_t KTRACE_STATE;

ssize_t ktrace_read_user(void* ptr, uint32_t off, size_t len) {
    ktrace_state_t* ks = &KTRACE_STATE;

    // Buffer size is limited by the marker if set,
    // otherwise limited by offset (last written point).
    // Offset can end up pointing past the end, so clip
    // it to the actual buffer size to be safe.
    uint32_t max;
    if (ks->marker) {
        max = ks->marker;
    } else {
        max = ks->offset;
        if (max > ks->bufsize) {
            max = ks->bufsize;
        }
    }

    // null read is a query for trace buffer size
    if (ptr == nullptr) {
        return max;
    }

    // constrain read to available buffer
    if (off >= max) {
        return 0;
    }
    if (len > (max - off)) {
        len = max - off;
    }

    if (arch_copy_to_user(ptr, ks->buffer + off, len) != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
    }
    return len;
}

zx_status_t ktrace_control(uint32_t action, uint32_t options, void* ptr) {
    ktrace_state_t* ks = &KTRACE_STATE;

    switch (action) {
    case KTRACE_ACTION_START:
        options = KTRACE_GRP_TO_MASK(options);
        ks->marker = 0;
        atomic_store(&ks->grpmask, options ? options : KTRACE_GRP_TO_MASK(KTRACE_GRP_ALL));
        ktrace_report_live_processes();
        ktrace_report_live_threads();
        break;

    case KTRACE_ACTION_STOP: {
        atomic_store(&ks->grpmask, 0);
        uint32_t n = ks->offset;
        if (n > ks->bufsize) {
            ks->marker = ks->bufsize;
        } else {
            ks->marker = n;
        }
        break;
    }

    case KTRACE_ACTION_REWIND:
        // roll back to just after the metadata
        atomic_store(&ks->offset, KTRACE_RECSIZE * 2);
        ktrace_report_syscalls(kt_syscall_info);
        ktrace_report_probes();
        ktrace_report_vcpu_meta();
        break;

    case KTRACE_ACTION_NEW_PROBE: {
        const char* const string_in = static_cast<const char*>(ptr);

        fbl::AutoLock lock(&probe_list_lock);
        StringRef* ref = ktrace_find_probe(string_in);
        if (ref != nullptr) {
            return ref->id;
        }

        struct DynamicStringRef {
            DynamicStringRef(const char* string)
                : string_ref{storage} {
                memcpy(storage, string, sizeof(storage));
            }

            StringRef string_ref;
            char storage[ZX_MAX_NAME_LEN];
        };

        // TODO(eieio,dje): Figure out how to constrain this to prevent abuse by
        // creating huge numbers of unique probes.
        fbl::AllocChecker alloc_checker;
        DynamicStringRef* dynamic_ref = new (&alloc_checker) DynamicStringRef{string_in};
        if (!alloc_checker.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        ktrace_add_probe(&dynamic_ref->string_ref);
        return dynamic_ref->string_ref.id;
    }

    default:
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

int trace_not_ready = 0;

void ktrace_init(unsigned level) {
    ktrace_state_t* ks = &KTRACE_STATE;

    uint32_t mb = cmdline_get_uint32("ktrace.bufsize", KTRACE_DEFAULT_BUFSIZE);
    uint32_t grpmask = cmdline_get_uint32("ktrace.grpmask", KTRACE_DEFAULT_GRPMASK);

    if (mb == 0) {
        dprintf(INFO, "ktrace: disabled\n");
        return;
    }

    mb *= (1024 * 1024);

    zx_status_t status;
    VmAspace* aspace = VmAspace::kernel_aspace();
    if ((status = aspace->Alloc("ktrace", mb, reinterpret_cast<void**>(&ks->buffer),
                                0, VmAspace::VMM_FLAG_COMMIT,
                                ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE)) < 0) {
        dprintf(INFO, "ktrace: cannot alloc buffer %d\n", status);
        return;
    }

    // The last packet written can overhang the end of the buffer,
    // so we reduce the reported size by the max size of a record
    ks->bufsize = mb - 256;

    dprintf(INFO, "ktrace: buffer at %p (%u bytes)\n", ks->buffer, mb);

    // write metadata to the first two event slots
    uint64_t n = ktrace_ticks_per_ms();
    ktrace_rec_32b_t* rec = reinterpret_cast<ktrace_rec_32b_t*>(ks->buffer);
    rec[0].tag = TAG_VERSION;
    rec[0].a = KTRACE_VERSION;
    rec[1].tag = TAG_TICKS_PER_MS;
    rec[1].a = static_cast<uint32_t>(n);
    rec[1].b = static_cast<uint32_t>(n >> 32);

    // enable tracing
    atomic_store(&ks->offset, KTRACE_RECSIZE * 2);
    ktrace_report_syscalls(kt_syscall_info);
    ktrace_report_probes();
    atomic_store(&ks->grpmask, KTRACE_GRP_TO_MASK(grpmask));

    // report names of existing threads
    ktrace_report_live_threads();

    // report metadata for VCPUs
    ktrace_report_vcpu_meta();

    // Report an event for "tracing is all set up now".  This also
    // serves to ensure that there will be at least one static probe
    // entry so that the __{start,stop}_ktrace_probe symbols above
    // will be defined by the linker.
    ktrace_probe(TraceAlways, TraceContext::Thread, "ktrace_ready"_stringref);
}

void ktrace_tiny(uint32_t tag, uint32_t arg) {
    ktrace_state_t* ks = &KTRACE_STATE;
    if (tag & atomic_load(&ks->grpmask)) {
        tag = (tag & 0xFFFFFFF0) | 2;
        int off;
        if ((off = atomic_add(&ks->offset, KTRACE_HDRSIZE)) >= static_cast<int>(ks->bufsize)) {
            // if we arrive at the end, stop
            atomic_store(&ks->grpmask, 0);
        } else {
            ktrace_header_t* hdr = reinterpret_cast<ktrace_header_t*>(ks->buffer + off);
            hdr->ts = ktrace_timestamp();
            hdr->tag = tag;
            hdr->tid = arg;
        }
    }
}

void* ktrace_open(uint32_t tag) {
    ktrace_state_t* ks = &KTRACE_STATE;
    if (!(tag & atomic_load(&ks->grpmask))) {
        return nullptr;
    }

    int off;
    if ((off = atomic_add(&ks->offset, KTRACE_LEN(tag))) >= static_cast<int>(ks->bufsize)) {
        // if we arrive at the end, stop
        atomic_store(&ks->grpmask, 0);
        return nullptr;
    }

    ktrace_header_t* hdr = reinterpret_cast<ktrace_header_t*>(ks->buffer + off);
    hdr->ts = ktrace_timestamp();
    hdr->tag = tag;
    hdr->tid = KTRACE_FLAGS(tag) & KTRACE_FLAGS_CPU
                   ? arch_curr_cpu_num()
                   : static_cast<uint32_t>(get_current_thread()->user_tid);
    return hdr + 1;
}

void ktrace_name_etc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always) {
    ktrace_state_t* ks = &KTRACE_STATE;
    if ((tag & atomic_load(&ks->grpmask)) || always) {
        const uint32_t len = static_cast<uint32_t>(strnlen(name, ZX_MAX_NAME_LEN - 1));

        // set size to: sizeof(hdr) + len + 1, round up to multiple of 8
        tag = (tag & 0xFFFFFFF0) | ((KTRACE_NAMESIZE + len + 1 + 7) >> 3);

        int off;
        if ((off = atomic_add(&ks->offset, KTRACE_LEN(tag))) >= static_cast<int>(ks->bufsize)) {
            // if we arrive at the end, stop
            atomic_store(&ks->grpmask, 0);
        } else {
            ktrace_rec_name_t* rec = reinterpret_cast<ktrace_rec_name_t*>(ks->buffer + off);
            rec->tag = tag;
            rec->id = id;
            rec->arg = arg;
            memcpy(rec->name, name, len);
            rec->name[len] = 0;
        }
    }
}

LK_INIT_HOOK(ktrace, ktrace_init, LK_INIT_LEVEL_USER);
