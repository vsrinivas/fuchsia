// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/magenta.h>

#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/mutex.h>

#include <lk/init.h>

#include <lib/console.h>

#include <magenta/dispatcher.h>
#include <magenta/handle.h>
#include <magenta/process_dispatcher.h>
#include <magenta/state_tracker.h>

// The next two includes should be removed. See DeleteHandle().
#include <magenta/pci_interrupt_dispatcher.h>
#include <magenta/io_mapping_dispatcher.h>

#include <utils/arena.h>
#include <utils/intrusive_double_list.h>
#include <utils/type_support.h>

#define LOCAL_TRACE 0

// This handle limit relects the fact that VMOs have an internal array
// with entry per memory page. When we switch to a tree of ranges we can
// up this limit.
constexpr size_t kMaxHandleCount = 32 * 1024;

// The handle arena and its mutex.
mutex_t handle_mutex = MUTEX_INITIAL_VALUE(handle_mutex);
utils::TypedArena<Handle> handle_arena;

// The system exception handler
static utils::RefPtr<Dispatcher> system_exception_handler;
static mx_exception_behaviour_t system_exception_behaviour;
static mutex_t system_exception_mutex = MUTEX_INITIAL_VALUE(system_exception_mutex);

void magenta_init(uint level) {
    handle_arena.Init("handles", kMaxHandleCount);
    system_exception_behaviour = MX_EXCEPTION_BEHAVIOUR_DEFAULT;
}

Handle* MakeHandle(utils::RefPtr<Dispatcher> dispatcher, mx_rights_t rights) {
    AutoLock lock(&handle_mutex);
    return handle_arena.New(utils::move(dispatcher), rights);
}

Handle* DupHandle(Handle* source, mx_rights_t rights) {
    AutoLock lock(&handle_mutex);
    return handle_arena.New(source, rights);
}

void DeleteHandle(Handle* handle) {
    StateTracker* state_tracker = handle->dispatcher()->get_state_tracker();
    if (state_tracker) {
        state_tracker->Cancel(handle);
    } else {
        auto disp = handle->dispatcher();
        // This code is sad but necessary because certain dispatchers
        // have complicated Close() logic which cannot be untangled at
        // this time.
        switch (disp->GetType()) {
            case MX_OBJ_TYPE_PCI_INT: disp->get_pci_interrupt_dispatcher()->Close();
                break;
            case MX_OBJ_TYPE_IOMAP: disp->get_io_mapping_dispatcher()->Close();
                break;
            default:  break;
                // This is fine. See for example the LogDispatcher.
        };
    }

    handle->~Handle();
    AutoLock lock(&handle_mutex);
    handle_arena.RawFree(handle);
}

bool HandleInRange(void* addr) {
    AutoLock lock(&handle_mutex);
    return handle_arena.in_range(addr);
}

uint32_t MapHandleToU32(Handle* handle) {
    auto va = reinterpret_cast<char*>(handle) - reinterpret_cast<char*>(handle_arena.start());
    return static_cast<uint32_t>(va);
}

Handle* MapU32ToHandle(uint32_t value) {
    auto va = value + reinterpret_cast<char*>(handle_arena.start());
    if (value % sizeof(Handle) != 0)
        return nullptr;
    if (!HandleInRange(va))
        return nullptr;
    return reinterpret_cast<Handle*>(va);
}

void SetSystemExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour) {
    AutoLock lock(&system_exception_mutex);

    system_exception_handler = handler;
    system_exception_behaviour = behaviour;
}

utils::RefPtr<Dispatcher> GetSystemExceptionHandler() {
    AutoLock lock(&system_exception_mutex);
    return system_exception_handler;
}

bool magenta_rights_check(mx_rights_t actual, mx_rights_t desired) {
    if ((actual & desired) == desired)
        return true;
    LTRACEF("rights check fail!! has 0x%x, needs 0x%x\n", actual, desired);
    return false;
}

mx_status_t magenta_sleep(mx_time_t nanoseconds) {
    lk_time_t t = mx_time_to_lk(nanoseconds);
    if ((nanoseconds > 0ull) && (t == 0u))
        t = 1u;

    /* sleep with interruptable flag set */
    return thread_sleep_etc(t, true);
}

static int cmd_magenta(int argc, const cmd_args* argv) {
    int rc = 0;

    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments:\n");
    usage:
        printf("%s ps  : list processes\n", argv[0].str);
        printf("%s ps  help: display keymap\n", argv[0].str);
        return -1;
    }

    if (strcmp(argv[1].str, "ps") == 0) {
        if ((argc == 3) && (strcmp(argv[2].str, "help") == 0)) {
            ProcessDispatcher::DumpProcessListKeyMap();
        } else {
            ProcessDispatcher::DebugDumpProcessList();
        }
    } else {
        printf("unrecognized subcommand\n");
        goto usage;
    }
    return rc;
}

LK_INIT_HOOK(magenta, magenta_init, LK_INIT_LEVEL_THREADING);

STATIC_COMMAND_START
STATIC_COMMAND("mx", "magenta information", &cmd_magenta)
STATIC_COMMAND_END(mx);
