// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// N.B. The offline symbolizer (scripts/symbolize) reads our output,
// don't break it.

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <backtrace/backtrace.h>

#include <ngunwind/libunwind.h>
#include <ngunwind/fuchsia.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <mxalloc/new.h>

#include <mxtl/array.h>

#include "backtrace.h"
#include "dso-list.h"
#include "utils.h"

// Keep open debug info for this many files.
constexpr size_t kDebugInfoCacheNumWays = 2;

// Error callback for libbacktrace.

static void
bt_error_callback(void* vdata, const char* msg, int errnum) {
    fprintf(stderr, "%s", msg);
    if (errnum > 0)
        fprintf(stderr, ": %s", strerror (errnum));
    fprintf(stderr, "\n");
}

// backtrace_so_iterator function.
// We don't use libbacktrace to do the unwinding, we only use it to get
// file,line#,function_name for each pc. Therefore we don't need it to
// iterate over all shared libs.

static int
bt_so_iterator (void* iter_state, backtrace_so_callback* callback, void* data) {
    // Return non-zero so iteration stops.
    return 1;
}

// A cache of data stored for each shared lib.
// This lets us lazily obtain debug info, and only keep
// a subset of it in memory.
class DebugInfoCache {
 public:
    DebugInfoCache(dsoinfo_t* dso_list, size_t nr_ways);
    ~DebugInfoCache();

    dsoinfo_t* dso_list() { return dso_list_; }

    mx_status_t GetDebugInfo(uintptr_t pc, dsoinfo_t** out_dso, backtrace_state** out_bt_state);
    
 private:
    dsoinfo_t* dso_list_;

    size_t last_used_ = 0;

    bool cache_avail_ = false;

    struct way {
        // Not owned by us. This is the "tag".
        dsoinfo_t* dso = nullptr;
        // Owned by us.
        backtrace_state* bt_state = nullptr;
    };

    mxtl::Array<way> ways_;
};

// Note: We take ownership of |dso_list|.

DebugInfoCache::DebugInfoCache(dsoinfo_t* dso_list, size_t nr_ways)
    : dso_list_(dso_list) {
    AllocChecker ac;
    auto ways = new (&ac) way[nr_ways];
    if (!ac.check()) {
        debugf(1, "unable to initialize debug info cache\n");
        return;
    }
    ways_.reset(ways, nr_ways);
    cache_avail_ = true;
}

DebugInfoCache::~DebugInfoCache() {
    if (cache_avail_) {
        for (size_t i = 0; i < ways_.size(); ++i) {
            backtrace_destroy_state(ways_[i].bt_state, bt_error_callback, nullptr);
        }
    }

    dso_free_list(dso_list_);
}

// Find the DSO and debug info (backtrace_state) for PC.
// Returns ERR_NOT_FOUND if |pc| is not in any DSO.
// Otherwise the result is NO_ERROR, even if there is no extended debug
// info for libbacktrace (e.g., -g1 info).
// If the result is NO_ERROR then |*out_dso| is set.
// If the result is NO_ERROR then |*out_bt_state| is set to the
// accompanying libbacktrace state if available or nullptr if not.

mx_status_t DebugInfoCache::GetDebugInfo(uintptr_t pc,
                                         dsoinfo_t** out_dso,
                                         backtrace_state** out_bt_state) {
    dsoinfo_t* dso = dso_lookup(dso_list_, pc);
    if (dso == nullptr) {
        debugf(1, "No DSO found for pc %p\n", (void*) pc);
        return ERR_NOT_FOUND;
    }

#if 1 // Skip using libbacktrace until leaks are fixed.
    *out_dso = dso;
    *out_bt_state = nullptr;
    return NO_ERROR;
#endif

    // If we failed to initialize the cache (OOM) we can still report the
    // DSO we found.
    if (!cache_avail_) {
        *out_dso = dso;
        *out_bt_state = nullptr;
        return NO_ERROR;
    }

    const size_t nr_ways = ways_.size();

    for (size_t i = 0; i < nr_ways; ++i) {
        if (ways_[i].dso == dso) {
            debugf(1, "using cached debug info entry for pc %p\n", (void*) pc);
            *out_dso = dso;
            *out_bt_state = ways_[i].bt_state;
            return NO_ERROR;
        }
    }

    // PC is in a DSO, but not found in the cache.
    // N.B. From this point on the result is NO_ERROR.
    // If there is an "error" the user can still print something (and there's
    // no point in having error messages pollute the backtrace, at least by
    // default).

    *out_dso = dso;
    *out_bt_state = nullptr;

    const char* debug_file = nullptr;
    auto status = dso_find_debug_file(dso, &debug_file);
    if (status != NO_ERROR) {
        // There's no additional debug file available, but we did find the DSO.
        return NO_ERROR;
    }

    struct backtrace_state* bt_state =
        backtrace_create_state(debug_file, 0 /*!threaded*/,
                               bt_error_callback, nullptr);
    if (bt_state == nullptr) {
        debugf(1, "backtrace_create_state failed (OOM)\n");
        return NO_ERROR;
    }

    // last_used_+1: KISS until there's data warranting something better
    size_t way = (last_used_ + 1) % nr_ways;
    if (ways_[way].dso != nullptr) {
        // Free the entry.
        backtrace_destroy_state(ways_[way].bt_state, bt_error_callback, nullptr);
        ways_[way].dso = nullptr;
        ways_[way].bt_state = nullptr;
    }

    // The iterator doesn't do anything, but we pass |list| anyway
    // in case some day we need it to.
    backtrace_set_so_iterator(bt_state, bt_so_iterator, dso_list_);
    backtrace_set_base_address(bt_state, dso->base);

    ways_[way].dso = dso;
    ways_[way].bt_state = bt_state;
    *out_bt_state = bt_state;
    last_used_ = way;
    return NO_ERROR;
}

// Data to pass back from backtrace_pcinfo.
// We don't use libbacktrace to print the backtrace, we only use it to
// obtain file,line#,function_name.

struct bt_pcinfo_data
{
    const char* filename;
    int lineno;
    const char* function;
};

// Callback invoked by libbacktrace.

static int
btprint_callback(void* vdata, uintptr_t pc, const char* filename, int lineno,
                  const char* function) {
    auto data = reinterpret_cast<bt_pcinfo_data*> (vdata);

    data->filename = filename;
    data->lineno = lineno;
    data->function = function;

    return 0;
}

static void btprint(DebugInfoCache* di_cache, int n, uintptr_t pc, uintptr_t sp) {
    dsoinfo_t* dso;
    backtrace_state* bt_state;
    auto status = di_cache->GetDebugInfo(pc, &dso, &bt_state);

    if (status != NO_ERROR) {
        // The pc is not in any DSO.
        printf("bt#%02d: pc %p sp %p\n",
               n, (void*) pc, (void*) sp);
        return;
    }

    // Try to use libbacktrace if we can.

    struct bt_pcinfo_data pcinfo_data;
    memset(&pcinfo_data, 0, sizeof(pcinfo_data));

    if (bt_state != nullptr) {
        auto ret = backtrace_pcinfo(bt_state, pc, btprint_callback,
                                    bt_error_callback, &pcinfo_data);
        if (ret == 0) {
            // FIXME: How to interpret the result is seriously confusing.
            // There are cases where zero means failure and others where
            // zero means success. For now we just assume that pcinfo_data
            // will only be filled in on success.
        }
    }

    printf("bt#%02d: pc %p sp %p (%s,%p)",
           n, (void*) pc, (void*) sp, dso->name, (void*) (pc - dso->base));
    if (pcinfo_data.filename != nullptr && pcinfo_data.lineno > 0) {
        const char* base = cl_basename(pcinfo_data.filename);
        printf(" %s:%d", base, pcinfo_data.lineno);
    }
    if (pcinfo_data.function != nullptr)
        printf(" %s", pcinfo_data.function);
    printf("\n");
}

static int dso_lookup_for_unw(dsoinfo_t* dso_list, unw_word_t pc,
                              unw_word_t* base, const char** name) {
    dsoinfo_t* dso = dso_lookup(dso_list, pc);
    if (dso == nullptr)
        return 0;
    *base = dso->base;
    *name = dso->name;
    return 1;
}

void backtrace(mx_handle_t process, mx_handle_t thread,
               uintptr_t pc, uintptr_t sp, uintptr_t fp,
               bool use_libunwind) {
    // Prepend "app:" to the name we print for the process binary to tell the
    // reader (and the symbolize script!) that the name is the process's.
    // The name property is only 32 characters which may be insufficient.
    // N.B. The symbolize script looks for "app" and "app:".
#define PROCESS_NAME_PREFIX "app:"
#define PROCESS_NAME_PREFIX_LEN (sizeof(PROCESS_NAME_PREFIX) - 1)
    char name[MX_MAX_NAME_LEN + PROCESS_NAME_PREFIX_LEN];
    strcpy(name, PROCESS_NAME_PREFIX);
    auto status = mx_object_get_property(process, MX_PROP_NAME, name + PROCESS_NAME_PREFIX_LEN,
                                         sizeof(name) - PROCESS_NAME_PREFIX_LEN);
    if (status != NO_ERROR) {
        print_mx_error("mx_object_get_property, falling back to \"app\" for program name", status);
        strlcpy(name, "app", sizeof(name));
    }
    dsoinfo_t* dso_list = dso_fetch_list(process, name);

    dso_print_list(dso_list);

    // Set up libunwind if requested.

    bool libunwind_ok = use_libunwind;
    if (verbosity_level > 0) {
        // Don't turn on libunwind debugging for -d1.
        // Note: max libunwind debugging level is 16
        unw_set_debug_level(verbosity_level - 1);
    }

    unw_fuchsia_info_t* fuchsia = nullptr;
    unw_addr_space_t remote_as = nullptr;

    if (libunwind_ok) {
        fuchsia = unw_create_fuchsia(process, thread, dso_list, dso_lookup_for_unw);
        if (fuchsia == nullptr)
        {
            print_error("unw_fuchsia_create failed (OOM)");
            libunwind_ok = false;
        }
    }

    if (libunwind_ok) {
        remote_as =
            unw_create_addr_space((unw_accessors_t*) &_UFuchsia_accessors, 0);
        if (remote_as == nullptr)
        {
            print_error("unw_create_addr_space failed (OOM)");
            libunwind_ok = false;
        }
    }

    unw_cursor_t cursor;
    if (libunwind_ok) {
        int ret = unw_init_remote(&cursor, remote_as, fuchsia);
        if (ret < 0) {
            print_error("unw_init_remote failed (%d)", ret);
            libunwind_ok = false;
        }
    }

    if (!libunwind_ok) {
        print_error("Unable to initialize libunwind.");
        print_error("Falling back on heuristics which likely won't work");
        print_error("with optimized code.");
    }

    // TODO: Handle libunwind not finding .eh_frame in which case fallback
    // on using heuristics. Ideally this would be handled on a per-DSO basis.

    // Keep a cache of loaded debug info to maintain some performance
    // without loading debug info for all shared libs.
    // This won't get used if initializing libunwind failed, but we can still
    // use |dso_list|.
    DebugInfoCache di_cache(dso_list, kDebugInfoCacheNumWays);

    // On with the show.

    int n = 1;
    btprint(&di_cache, n++, pc, sp);
    while ((sp >= 0x1000000) && (n < 50)) {
        if (libunwind_ok) {
            int ret = unw_step(&cursor);
            if (ret < 0) {
                print_error("unw_step failed for pc %p, aborting backtrace here",
                            (void*) pc);
                break;
            }
            if (ret == 0)
                break;
            unw_word_t val;
            unw_get_reg(&cursor, UNW_REG_IP, &val);
            pc = val;
            unw_get_reg(&cursor, UNW_REG_SP, &val);
            sp = val;
        } else {
            sp = fp;
            if (read_mem(process, fp + 8, &pc, sizeof(pc))) {
                break;
            }
            if (read_mem(process, fp, &fp, sizeof(fp))) {
                break;
            }
        }
        btprint(&di_cache, n++, pc, sp);
    }
    printf("bt#%02d: end\n", n);

    unw_destroy_addr_space(remote_as);
    unw_destroy_fuchsia(fuchsia);
}
