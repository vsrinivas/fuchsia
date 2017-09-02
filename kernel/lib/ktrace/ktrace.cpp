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
#include <kernel/cmdline.h>
#include <vm/vm_aspace.h>
#include <lib/ktrace.h>
#include <lk/init.h>
#include <magenta/thread_annotations.h>
#include <object/thread_dispatcher.h>

#if __x86_64__
#define ktrace_timestamp() rdtsc();
#define ktrace_ticks_per_ms() (ticks_per_second() / 1000)
#else
#define ktrace_timestamp() current_time()
#define ktrace_ticks_per_ms() (1000000)
#endif

static void ktrace_name_etc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always);

// Generated struct that has the syscall index and name.
static struct ktrace_syscall_info {
    uint32_t id;
    uint32_t nargs;
    const char* name;
} kt_syscall_info [] = {
    #include <magenta/syscall-ktrace-info.inc>
    {0, 0, nullptr}
};

void ktrace_report_syscalls(ktrace_syscall_info* call) {
    size_t ix = 0;
    while (call[ix].name) {
        ktrace_name_etc(TAG_SYSCALL_NAME, call[ix].id, 0, call[ix].name, true);
        ++ix;
    }
}

static uint32_t probe_number = 1;

extern ktrace_probe_info_t __start_ktrace_probe[] __WEAK;
extern ktrace_probe_info_t __stop_ktrace_probe[] __WEAK;

static mutex_t probe_list_lock = MUTEX_INITIAL_VALUE(probe_list_lock);
static ktrace_probe_info_t* probe_list TA_GUARDED(probe_list_lock);

static ktrace_probe_info_t* ktrace_find_probe(const char* name) TA_REQ(probe_list_lock) {
    ktrace_probe_info_t* probe;
    for (probe = probe_list; probe != nullptr; probe = probe->next) {
        if (!strcmp(name, probe->name)) {
            return probe;
        }
    }
    return nullptr;
}

static void ktrace_add_probe(ktrace_probe_info_t* probe) TA_REQ(probe_list_lock) {
    if (probe->num == 0) {
        probe->num = probe_number++;
    }
    probe->next = probe_list;
    probe_list = probe;
    ktrace_name_etc(TAG_PROBE_NAME, probe->num, 0, probe->name, true);
}

static void ktrace_report_probes(void) {
    ktrace_probe_info_t *probe;
    mutex_acquire(&probe_list_lock);
    for (probe = probe_list; probe != nullptr; probe = probe->next) {
        ktrace_name_etc(TAG_PROBE_NAME, probe->num, 0, probe->name, true);
    }
    mutex_release(&probe_list_lock);
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

int ktrace_read_user(void* ptr, uint32_t off, uint32_t len) {
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

    if (arch_copy_to_user(ptr, ks->buffer + off, len) != MX_OK) {
        return MX_ERR_INVALID_ARGS;
    }
    return len;
}

mx_status_t ktrace_control(uint32_t action, uint32_t options, void* ptr) {
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
        break;
    case KTRACE_ACTION_NEW_PROBE: {
        ktrace_probe_info_t* probe;
        mutex_acquire(&probe_list_lock);
        if ((probe = ktrace_find_probe((const char*) ptr)) != nullptr) {
            mutex_release(&probe_list_lock);
            return probe->num;
        }
        probe = (ktrace_probe_info_t*) calloc(sizeof(*probe) + MX_MAX_NAME_LEN, 1);
        if (probe == nullptr) {
            mutex_release(&probe_list_lock);
            return MX_ERR_NO_MEMORY;
        }
        probe->name = (const char*) (probe + 1);
        memcpy(probe + 1, ptr, MX_MAX_NAME_LEN);
        ktrace_add_probe(probe);
        mutex_release(&probe_list_lock);
        return probe->num;
    }
    default:
        return MX_ERR_INVALID_ARGS;
    }
    return MX_OK;
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

    mb *= (1024*1024);

    mx_status_t status;
    VmAspace* aspace = VmAspace::kernel_aspace();
    if ((status = aspace->Alloc("ktrace", mb, (void**)&ks->buffer, 0, VmAspace::VMM_FLAG_COMMIT,
                                ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE)) < 0) {
        dprintf(INFO, "ktrace: cannot alloc buffer %d\n", status);
        return;
    }

    // The last packet written can overhang the end of the buffer,
    // so we reduce the reported size by the max size of a record
    ks->bufsize = mb - 256;

    dprintf(INFO, "ktrace: buffer at %p (%u bytes)\n", ks->buffer, mb);

    // register all static probes
    ktrace_probe_info_t *probe;
    mutex_acquire(&probe_list_lock);
    for (probe = __start_ktrace_probe; probe != __stop_ktrace_probe; probe++) {
        ktrace_add_probe(probe);
    }
    mutex_release(&probe_list_lock);

    // write metadata to the first two event slots
    uint64_t n = ktrace_ticks_per_ms();
    ktrace_rec_32b_t* rec = (ktrace_rec_32b_t*) ks->buffer;
    rec[0].tag = TAG_VERSION;
    rec[0].a = KTRACE_VERSION;
    rec[1].tag = TAG_TICKS_PER_MS;
    rec[1].a = (uint32_t)n;
    rec[1].b = (uint32_t)(n >> 32);

    // enable tracing
    atomic_store(&ks->offset, KTRACE_RECSIZE * 2);
    ktrace_report_syscalls(kt_syscall_info);
    ktrace_report_probes();
    atomic_store(&ks->grpmask, KTRACE_GRP_TO_MASK(grpmask));

    // report names of existing threads
    ktrace_report_live_threads();
}

void ktrace_tiny(uint32_t tag, uint32_t arg) {
    ktrace_state_t* ks = &KTRACE_STATE;
    if (tag & atomic_load(&ks->grpmask)) {
        tag = (tag & 0xFFFFFFF0) | 2;
        int off;
        if ((off = atomic_add(&ks->offset, KTRACE_HDRSIZE)) >= (int)ks->bufsize) {
            // if we arrive at the end, stop
            atomic_store(&ks->grpmask, 0);
        } else {
            ktrace_header_t* hdr = (ktrace_header_t*) (ks->buffer + off);
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
    if ((off = atomic_add(&ks->offset, KTRACE_LEN(tag))) >= (int)ks->bufsize) {
        // if we arrive at the end, stop
        atomic_store(&ks->grpmask, 0);
        return nullptr;
    }

    ktrace_header_t* hdr = (ktrace_header_t*) (ks->buffer + off);
    hdr->ts = ktrace_timestamp();
    hdr->tag = tag;
    hdr->tid = (uint32_t)get_current_thread()->user_tid;
    return hdr + 1;
}

static void ktrace_name_etc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always) {
    ktrace_state_t* ks = &KTRACE_STATE;
    if ((tag & atomic_load(&ks->grpmask)) || always) {
        uint32_t len = static_cast<uint32_t>(strnlen(name, 31));

        // set size to: sizeof(hdr) + len + 1, round up to multiple of 8
        tag = (tag & 0xFFFFFFF0) | ((KTRACE_NAMESIZE + len + 1 + 7) >> 3);

        int off;
        if ((off = atomic_add(&ks->offset, KTRACE_LEN(tag))) >= (int)ks->bufsize) {
            // if we arrive at the end, stop
            atomic_store(&ks->grpmask, 0);
        } else {
            ktrace_rec_name_t* rec = (ktrace_rec_name_t*) (ks->buffer + off);
            rec->tag = tag;
            rec->id = id;
            rec->arg = arg;
            memcpy(rec->name, name, len);
            rec->name[len] = 0;
        }
    }
}

void ktrace_name(uint32_t tag, uint32_t id, uint32_t arg, const char* name) {
    ktrace_name_etc(tag, id, arg, name, false);
}

LK_INIT_HOOK(ktrace, ktrace_init, LK_INIT_LEVEL_APPS - 1);
