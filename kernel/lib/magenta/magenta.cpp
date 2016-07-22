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
#include <utils/list_utils.h>
#include <utils/type_support.h>

#define LOCAL_TRACE 0

// This handle limit relects the fact that VMOs have an internal array
// with entry per memory page. When we switch to a tree of ranges we can
// up this limit.
constexpr size_t kMaxHandleCount = 32 * 1024;

// The handle arena and its mutex.
mutex_t handle_mutex = MUTEX_INITIAL_VALUE(handle_mutex);
utils::TypedArena<Handle> handle_arena;

// The process list, id and its mutex.
mutex_t process_mutex = MUTEX_INITIAL_VALUE(process_mutex);
uint32_t next_process_id = 0u;
utils::DoublyLinkedList<ProcessDispatcher*> process_list;

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

uint32_t AddProcess(ProcessDispatcher* process) {
    // Don't call any method of |process|, it is not yet fully constructed.
    AutoLock lock(&process_mutex);
    ++next_process_id;
    process_list.push_back(process);
    LTRACEF("Adding process %p : id = %u\n", process, next_process_id);
    return next_process_id;
}

void RemoveProcess(ProcessDispatcher* process) {
    AutoLock lock(&process_mutex);
    process_list.erase(process);
    LTRACEF("Removing process %p : id = %u\n", process, process->id());
}

char* DebugDumpHandleTypeCount_NoLock(const ProcessDispatcher& process) {
    static char buf[(MX_OBJ_TYPE_LAST * 4) + 1];

    uint32_t types[MX_OBJ_TYPE_LAST] = {0};
    uint32_t handle_count = process.HandleStats(types, sizeof(types));

    snprintf(buf, sizeof(buf), "%3u: %3u %3u %3u %3u %3u %3u %3u %3u %3u",
             handle_count,
             types[1],              // process.
             types[2],              // thread.
             types[3],              // vmem
             types[4],              // msg pipe.
             types[5],              // event
             types[6],              // ioport.
             types[7] + types[8],   // data pipe (both),
             types[9],              // interrupt.
             types[10]              // io map
             );
    return buf;
}

void DebugDumpProcessList() {
    AutoLock lock(&process_mutex);
    printf(" id-s  #t  #h:  #pr #th #vm #mp #ev #ip #dp #it #io[name]\n");
    for (const auto& process : process_list) {
        printf("%3u-%c %3u %s [%s]\n",
            process.id(),
            process.StateChar(),
            process.ThreadCount(),
            DebugDumpHandleTypeCount_NoLock(process),
            process.name().data());
    }
}

void DumpProcessListKeyMap() {
    printf("id  : process id number\n");
    printf("-s  : state: R = running D = dead\n");
    printf("#t  : number of threads\n");
    printf("#h  : total number of handles\n");
    printf("#pr : number of process handles\n");
    printf("#th : number of thread handles\n");
    printf("#vm : number of vm map handles\n");
    printf("#mp : number of message pipe handles\n");
    printf("#ev : number of event handles\n");
    printf("#ip : number of io port handles\n");
    printf("#dp : number of data pipe handles (both)\n");
    printf("#it : number of interrupt handles\n");
    printf("#io : number of io map handles\n");
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
            DumpProcessListKeyMap();
        } else {
            DebugDumpProcessList();
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
