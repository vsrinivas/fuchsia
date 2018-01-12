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

#include <zircon/types.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>

#include "inspector/inspector.h"
#include "dso-list-impl.h"
#include "utils-impl.h"

namespace inspector {

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
    DebugInfoCache(inspector_dsoinfo_t* dso_list, size_t nr_ways);
    ~DebugInfoCache();

    inspector_dsoinfo_t* dso_list() { return dso_list_; }

    zx_status_t GetDebugInfo(uintptr_t pc, inspector_dsoinfo_t** out_dso,
                             backtrace_state** out_bt_state);
    
 private:
    inspector_dsoinfo_t* dso_list_;

    size_t last_used_ = 0;

    bool cache_avail_ = false;

    struct way {
        // Not owned by us. This is the "tag".
        inspector_dsoinfo_t* dso = nullptr;
        // Owned by us.
        backtrace_state* bt_state = nullptr;
    };

    fbl::Array<way> ways_;
};

// Note: We *do not* take ownership of |dso_list|.
// Its lifetime must survive ours.

DebugInfoCache::DebugInfoCache(inspector_dsoinfo_t* dso_list, size_t nr_ways)
    : dso_list_(dso_list) {
    fbl::AllocChecker ac;
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
}

// Find the DSO and debug info (backtrace_state) for PC.
// Returns ZX_ERR_NOT_FOUND if |pc| is not in any DSO.
// Otherwise the result is ZX_OK, even if there is no extended debug
// info for libbacktrace (e.g., -g1 info).
// If the result is ZX_OK then |*out_dso| is set.
// If the result is ZX_OK then |*out_bt_state| is set to the
// accompanying libbacktrace state if available or nullptr if not.

zx_status_t DebugInfoCache::GetDebugInfo(uintptr_t pc,
                                         inspector_dsoinfo_t** out_dso,
                                         backtrace_state** out_bt_state) {
    inspector_dsoinfo_t* dso = inspector_dso_lookup(dso_list_, pc);
    if (dso == nullptr) {
        debugf(1, "No DSO found for pc %p\n", (void*) pc);
        return ZX_ERR_NOT_FOUND;
    }

#if 1 // Skip using libbacktrace until leaks are fixed. ZX-351
    *out_dso = dso;
    *out_bt_state = nullptr;
    return ZX_OK;
#endif

    // If we failed to initialize the cache (OOM) we can still report the
    // DSO we found.
    if (!cache_avail_) {
        *out_dso = dso;
        *out_bt_state = nullptr;
        return ZX_OK;
    }

    const size_t nr_ways = ways_.size();

    for (size_t i = 0; i < nr_ways; ++i) {
        if (ways_[i].dso == dso) {
            debugf(1, "using cached debug info entry for pc %p\n", (void*) pc);
            *out_dso = dso;
            *out_bt_state = ways_[i].bt_state;
            return ZX_OK;
        }
    }

    // PC is in a DSO, but not found in the cache.
    // N.B. From this point on the result is ZX_OK.
    // If there is an "error" the user can still print something (and there's
    // no point in having error messages pollute the backtrace, at least by
    // default).

    *out_dso = dso;
    *out_bt_state = nullptr;

    const char* debug_file = nullptr;
    auto status = inspector_dso_find_debug_file(dso, &debug_file);
    if (status != ZX_OK) {
        // There's no additional debug file available, but we did find the DSO.
        return ZX_OK;
    }

    struct backtrace_state* bt_state =
        backtrace_create_state(debug_file, 0 /*!threaded*/,
                               bt_error_callback, nullptr);
    if (bt_state == nullptr) {
        debugf(1, "backtrace_create_state failed (OOM)\n");
        return ZX_OK;
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
    return ZX_OK;
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

static void btprint(FILE* f, DebugInfoCache* di_cache,
                    int n, uintptr_t pc, uintptr_t sp) {
    inspector_dsoinfo_t* dso;
    backtrace_state* bt_state;
    auto status = di_cache->GetDebugInfo(pc, &dso, &bt_state);

    if (status != ZX_OK) {
        // The pc is not in any DSO.
        fprintf(f, "bt#%02d: pc %p sp %p\n",
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

    fprintf(f, "bt#%02d: pc %p sp %p (%s,%p)",
            n, (void*) pc, (void*) sp, dso->name, (void*) (pc - dso->base));
    if (pcinfo_data.filename != nullptr && pcinfo_data.lineno > 0) {
        const char* base = path_basename(pcinfo_data.filename);
        // Be paranoid and handle |pcinfo_data.filename| having a trailing /.
        // If so, just print the whole thing and let the user figure it out.
        if (*base == '\0')
            base = pcinfo_data.filename;
        fprintf(f, " %s:%d", base, pcinfo_data.lineno);
    }
    if (pcinfo_data.function != nullptr)
        fprintf(f, " %s", pcinfo_data.function);
    fprintf(f, "\n");
}

static int dso_lookup_for_unw(void* context, unw_word_t pc,
                              unw_word_t* base, const char** name) {
    auto dso_list = reinterpret_cast<inspector_dsoinfo_t*>(context);
    inspector_dsoinfo_t* dso = inspector_dso_lookup(dso_list, pc);
    if (dso == nullptr)
        return 0;
    *base = dso->base;
    *name = dso->name;
    return 1;
}

extern "C"
void inspector_print_backtrace(FILE* f,
                               zx_handle_t process, zx_handle_t thread,
                               inspector_dsoinfo_t* dso_list,
                               uintptr_t pc, uintptr_t sp, uintptr_t fp,
                               bool use_libunwind) {
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
    btprint(f, &di_cache, n++, pc, sp);
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
        btprint(f, &di_cache, n++, pc, sp);
    }
    fprintf(f, "bt#%02d: end\n", n);

    unw_destroy_addr_space(remote_as);
    unw_destroy_fuchsia(fuchsia);
}

}  // namespace inspector
