// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// memgraph prints system-wide task and memory information as JSON.
// See memgraph-schema.json for the schema.

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <magenta/process.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/object.h>
#include <mxcpp/new.h>
#include <task-utils/walker.h>

#include "resources.h"
#include "vmo-utils.h"

// Defines kMemgraphSchema containing the contents of memgraph-schema.json
#include "memgraph-schema.h"

namespace {

// Prints info about VMOs and their relationship to a process.
// Assumes we're in the middle of dumping a process.
// TODO(dbort): Insert some special entry if count < avail?
void print_vmos(const mx_info_vmo_t* vmos, size_t count) {
    if (count == 0) {
        // Should never happen, but don't print anything in this case.
        return;
    }

    // List of VMOs that this task points to. Should only contain fields that
    // are fundamental parts of the VMO and do not change based on how the VMO
    // is used or referred to.
    printf(",\n   \"vmos\": [\n");
    for (size_t i = 0; i < count; i++) {
        const mx_info_vmo_t* vmo = vmos + i;
        char delim = i < (count - 1) ? ',' : ' ';
        printf("      {"
               "\"koid\": %" PRIu64 ", "
               "\"name\": \"%s\", " // TODO(dbort): Escape quotes
               "\"size_bytes\": %" PRIu64 ", "
               "\"parent_koid\": %" PRIu64 ", "
               "\"num_children\": %zu, "
               "\"num_mappings\": %zu, "
               "\"share_count\": %zu, "
               // TODO(dbort): copy-on-write? phys/paged?
               "\"committed_bytes\": %" PRIu64
               "}%c\n",
               vmo->koid,
               vmo->name,
               vmo->size_bytes,
               vmo->parent_koid,
               vmo->num_children,
               vmo->num_mappings,
               vmo->share_count,
               vmo->committed_bytes,
               delim);
    }
    printf("   ],\n");
    // List of references from this task to the VMOs listed above. May include
    // information specific to this particular use of a given VMO.
    printf("   \"vmo_refs\": [\n");
    for (size_t i = 0; i < count; i++) {
        const mx_info_vmo_t* vmo = vmos + i;
        char delim = i < (count - 1) ? ',' : ' ';
        printf("      {"
               "\"vmo_koid\": %" PRIu64 ", "
               "\"via\": [",
               vmo->koid);
        bool need_comma = false;
        if (vmo->flags & MX_INFO_VMO_VIA_HANDLE) {
            printf("\"HANDLE\"");
            need_comma = true;
        }
        if (vmo->flags & MX_INFO_VMO_VIA_MAPPING) {
            printf("%s\"MAPPING\"", need_comma ? ", " : "");
            // Future improvement: Could use MX_INFO_PROCESS_MAPS to include
            // specifics of how this VMO is mapped.
        }
        printf("]");
        if (vmo->flags & MX_INFO_VMO_VIA_HANDLE) {
            need_comma = false;
            printf(", \"handle_rights\": [");

#define PRINT_RIGHT(r)                                      \
    do {                                                    \
        if (vmo->handle_rights & MX_RIGHT_##r) {            \
            printf("%s\"" #r "\"", need_comma ? ", " : ""); \
            need_comma = true;                              \
        }                                                   \
    } while (false)

            PRINT_RIGHT(READ);
            PRINT_RIGHT(WRITE);
            PRINT_RIGHT(EXECUTE);
            PRINT_RIGHT(MAP);
            PRINT_RIGHT(DUPLICATE);
            PRINT_RIGHT(TRANSFER);

#undef PRINT_RIGHT

            printf("]");
        }
        printf("}%c\n", delim);
    }
    printf("   ]");
}

class JsonTaskEnumerator final : public TaskEnumerator {
public:
    // |self_koid| is the koid of this memgraph process, so we can
    // avoid trying to read our own VMOs (which is illegal).
    JsonTaskEnumerator(mx_koid_t self_koid, bool show_threads, bool show_vmos)
        : self_koid_(self_koid),
          show_threads_(show_threads), show_vmos_(show_vmos) {}

    mx_status_t partial_failure() const { return partial_failure_; }

private:
    static void GetTaskName(mx_handle_t task, mx_koid_t koid,
                            char out_name[MX_MAX_NAME_LEN]) {
        mx_status_t s = mx_object_get_property(
            task, MX_PROP_NAME, out_name, MX_MAX_NAME_LEN);
        if (s != MX_OK) {
            fprintf(stderr,
                    "WARNING: failed to get name of task %" PRIu64
                    ": %s (%d)\n",
                    koid, mx_status_get_string(s), s);
            snprintf(out_name, MX_MAX_NAME_LEN, "<UNKNOWN>");
            // This is unfortunate, but not worth a partial failure
            // since the overall structure of the output is still intact.
        }
        // TODO(dbort): Escape quotes in name
    }

    mx_status_t OnJob(int depth, mx_handle_t job,
                      mx_koid_t koid, mx_koid_t parent_koid) override {
        char name[MX_MAX_NAME_LEN];
        GetTaskName(job, koid, name);

        char parent_id[MX_MAX_NAME_LEN + 16];
        if (parent_koid == 0) {
            // This is the root job, which we treat as a child of the
            // system VMO arena node.
            snprintf(parent_id, sizeof(parent_id), "kernel/vmo");
        } else {
            snprintf(parent_id, sizeof(parent_id), "j/%" PRIu64, parent_koid);
        }

        printf("  {"
               "\"id\": \"j/%" PRIu64 "\", "
               "\"type\": \"j\", "
               "\"koid\": %" PRIu64 ", "
               "\"parent\": \"%s\", "
               "\"name\": \"%s\""
               "},\n",
               koid,
               koid,
               parent_id,
               name);

        return MX_OK;
    }

    mx_status_t OnProcess(int depth, mx_handle_t process,
                          mx_koid_t koid, mx_koid_t parent_koid) override {
        char name[MX_MAX_NAME_LEN];
        GetTaskName(process, koid, name);

        // Print basic info.
        printf("  {"
               "\"id\": \"p/%" PRIu64 "\", "
               "\"type\": \"p\", "
               "\"koid\": %" PRIu64 ", "
               "\"parent\": \"j/%" PRIu64 "\", "
               "\"name\": \"%s\"",
               koid,
               koid,
               parent_koid,
               name);

        // Print memory usage summaries.
        mx_info_task_stats_t info;
        mx_status_t s = mx_object_get_info(
            process, MX_INFO_TASK_STATS, &info, sizeof(info), nullptr, nullptr);
        if (s == MX_ERR_BAD_STATE) {
            // Process has exited, but has not been destroyed.
            // Default to zero for all sizes.
            info = {};
            s = MX_OK;
        }
        if (s != MX_OK) {
            fprintf(stderr,
                    "WARNING: failed to get mem stats for process %" PRIu64
                    ": %s (%d)\n",
                    koid, mx_status_get_string(s), s);
            set_partial_failure(s);
        } else {
            printf(", "
                   "\"private_bytes\": %zu, "
                   "\"shared_bytes\": %zu, "
                   "\"pss_bytes\": %zu",
                   info.mem_private_bytes,
                   info.mem_shared_bytes,
                   info.mem_private_bytes + info.mem_scaled_shared_bytes);
        }

        // Print the process's VMOs. The same VMO may appear several
        // times in this list; it's up to the consumer of this output
        // to de-duplicate.
        if (show_vmos_ && koid != self_koid_) {
            mx_info_vmo_t* vmos;
            size_t count = 0;
            size_t avail = 0;
            s = get_vmos(process, &vmos, &count, &avail);
            if (s != MX_OK) {
                fprintf(stderr,
                        "WARNING: failed to read VMOs for process %" PRIu64
                        ": %s (%d)\n",
                        koid, mx_status_get_string(s), s);
                set_partial_failure(s);
            } else {
                if (count < avail) {
                    fprintf(stderr,
                            "WARNING: failed to read all VMOs for process "
                            "%" PRIu64 ": count %zu < avail %zu\n",
                            koid, count, avail);
                    set_partial_failure(MX_ERR_BUFFER_TOO_SMALL);
                    // Keep going with the truncated list.
                }
                print_vmos(vmos, count);
                free(vmos);
            }
        }
        printf("},\n");

        return MX_OK;
    }

    mx_status_t OnThread(int depth, mx_handle_t thread,
                         mx_koid_t koid, mx_koid_t parent_koid) override {
        char name[MX_MAX_NAME_LEN];
        GetTaskName(thread, koid, name);

        // Print basic info.
        printf("  {"
               "\"id\": \"t/%" PRIu64 "\", "
               "\"type\": \"t\", "
               "\"koid\": %" PRIu64 ", "
               "\"parent\": \"p/%" PRIu64 "\", "
               "\"name\": \"%s\"",
               koid,
               koid,
               parent_koid,
               name);

        // Print state.
        mx_info_thread_t info;
        mx_status_t s = mx_object_get_info(
            thread, MX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
        if (s != MX_OK) {
            fprintf(stderr,
                    "WARNING: failed to get info for thread %" PRIu64
                    ": %s (%d)\n",
                    koid, mx_status_get_string(s), s);
            set_partial_failure(s);
        } else {
            const char* state = "<UNKNOWN>";
            if (info.wait_exception_port_type != MX_EXCEPTION_PORT_TYPE_NONE) {
                state = "EXCEPTION";
            } else {
                switch (info.state) {
                case MX_THREAD_STATE_NEW:
                    state = "NEW";
                    break;
                case MX_THREAD_STATE_RUNNING:
                    state = "RUNNING";
                    break;
                case MX_THREAD_STATE_SUSPENDED:
                    state = "SUSPENDED";
                    break;
                case MX_THREAD_STATE_BLOCKED:
                    state = "BLOCKED";
                    break;
                case MX_THREAD_STATE_DYING:
                    state = "DYING";
                    break;
                case MX_THREAD_STATE_DEAD:
                    state = "DEAD";
                    break;
                }
            }
            printf(", \"state\": \"%s\"", state);
        }
        printf("},\n");
        return MX_OK;
    }

    const mx_koid_t self_koid_;
    const bool show_threads_;
    const bool show_vmos_;

    // We try to keep going despite failures, but for scripting
    // purposes it's good to indicate failure at the end.
    void set_partial_failure(mx_status_t status) {
        if (partial_failure_ == MX_OK) {
            partial_failure_ = status;
        }
    }
    mx_status_t partial_failure_ = MX_OK;

    bool has_on_job() const final { return true; }
    bool has_on_process() const final { return true; }
    bool has_on_thread() const final { return show_threads_; }
};

static void print_kernel_json(const char* name, const char* parent,
                              uint64_t size_bytes) {
    printf("  {"
           "\"id\": \"kernel/%s\", "
           "\"type\": \"kernel\", "
           "\"parent\": \"%s\", "
           "\"name\": \"%s\", "
           "\"size_bytes\": %zu"
           "},\n",
           name,
           parent,
           name,
           size_bytes);
}

mx_status_t dump_kernel_memory() {
    mx_handle_t root_resource;
    mx_status_t s = get_root_resource(&root_resource);
    if (s != MX_OK) {
        return s;
    }
    mx_info_kmem_stats_t stats;
    s = mx_object_get_info(root_resource, MX_INFO_KMEM_STATS,
                           &stats, sizeof(stats), nullptr, nullptr);
    mx_handle_close(root_resource);
    if (s != MX_OK) {
        fprintf(stderr, "WARNING: failed to get kernel memory stats: %s (%d)\n",
                mx_status_get_string(s), s);
        return s;
    }

    print_kernel_json("physmem", "", stats.total_bytes);
    print_kernel_json("free", "kernel/physmem", stats.free_bytes);
    print_kernel_json("vmo", "kernel/physmem", stats.vmo_bytes);
    print_kernel_json("heap", "kernel/physmem", stats.total_heap_bytes);
    print_kernel_json("heap/allocated", "kernel/heap",
                      stats.total_heap_bytes - stats.free_heap_bytes);
    print_kernel_json("heap/free", "kernel/heap", stats.free_heap_bytes);
    print_kernel_json("wired", "kernel/physmem", stats.wired_bytes);
    print_kernel_json("mmu", "kernel/physmem", stats.mmu_overhead_bytes);
    print_kernel_json("other", "kernel/physmem", stats.other_bytes);

    return MX_OK;
}

void print_help(FILE* f) {
    fprintf(f, "Usage: memgraph [options]\n");
    fprintf(f, "  Prints system-wide task and memory info as JSON.\n");
    fprintf(f, "Options:\n");
    fprintf(f, " -t|--threads  Include threads in the output\n");
    fprintf(f, " -v|--vmos     Include VMOs in the output\n");
    fprintf(f, " -S|--schema   Print the schema for the JSON output format\n");
    fprintf(f, " -h|--help     Display this message\n");
}

} // namespace

int main(int argc, char** argv) {
    int show_threads = false;
    int show_vmos = false;
    while (true) {
        static option options[] = {
            {"threads", no_argument, nullptr, 't'},
            {"vmos", no_argument, nullptr, 'v'},
            {"schema", no_argument, nullptr, 'S'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int option_index = 0;
        int c = getopt_long(argc, argv, "tvSh", options, &option_index);
        if (c < 0) {
            break;
        }
        switch (c) {
        case 't':
            show_threads = true;
            break;
        case 'v':
            show_vmos = true;
            break;
        case 'S':
            printf(kMemgraphSchema);
            return 0;
        case 'h':
        default:
            print_help(c == 'h' ? stdout : stderr);
            return c == 'h' ? 0 : 1;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "%s: unrecognized extra arguments:", argv[0]);
        while (optind < argc) {
            fprintf(stderr, " %s", argv[optind++]);
        }
        fprintf(stderr, "\n");
        print_help(stderr);
        return 1;
    }

    // Get our own koid so we can avoid (illegally) reading this process's VMOs.
    mx_info_handle_basic_t info;
    mx_status_t s = mx_object_get_info(mx_process_self(), MX_INFO_HANDLE_BASIC,
                                       &info, sizeof(info), nullptr, nullptr);
    if (s != MX_OK) {
        // This will probably result in a partial failure when we try to read
        // our own VMOs, but keep going.
        fprintf(stderr, "WARNING: could not find our own koid: %s (%d)\n",
                mx_status_get_string(s), s);
        info = {};
        info.koid = 0;
    }

    // Grab the time when we start.
    struct timespec now;
    timespec_get(&now, TIME_UTC);

    printf("[\n");

    mx_status_t ks = dump_kernel_memory();

    JsonTaskEnumerator jte(info.koid, show_threads, show_vmos);
    s = jte.WalkRootJobTree();
    if (s != MX_OK) {
        fprintf(stderr, "ERROR: %s (%d)\n", mx_status_get_string(s), s);
        return 1;
    }

    // Add a final entry with metadata. Also lets us avoid tracking commas
    // above.
    // Print the time as an ISO 8601 string.
    struct tm nowtm;
    gmtime_r(&now.tv_sec, &nowtm);
    char tbuf[40];
    strftime(tbuf, sizeof(tbuf), "%FT%T", &nowtm);
    printf("  {"
           "\"type\": \"__META\", "
           "\"timestamp\": \"%s.%03ldZ\"}\n",
           tbuf, now.tv_nsec / (1000 * 1000));
    printf("]\n");

    // Exit with an error status if we hit any partial failures.
    s = jte.partial_failure();
    if (s == MX_OK) {
        s = ks;
    }
    if (s != MX_OK) {
        fprintf(stderr, "ERROR: delayed exit after partial failure: %s (%d)\n",
                mx_status_get_string(s), s);
        return 1;
    }
    return 0;
}
