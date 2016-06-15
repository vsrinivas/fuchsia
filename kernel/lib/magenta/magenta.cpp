// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <trace.h>

#include <magenta/magenta.h>

#include <lib/console.h>

#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <lk/init.h>

#include <magenta/dispatcher.h>
#include <magenta/handle.h>
#include <magenta/user_process.h>

#include <utils/arena.h>
#include <utils/intrusive_double_list.h>
#include <utils/list_utils.h>
#include <utils/type_support.h>

#define LOCAL_TRACE 0

// This handle limit is super low, but we can increase it when the arena
// can reserve VA ranges. Currently it commits physical pages.
constexpr size_t kMaxHandleCount = 8 * 1024;

// The handle arena and its mutex.
mutex_t handle_mutex;
utils::TypedArena<Handle> handle_arena;

// The process list, id and its mutex.
mutex_t process_mutex;
uint32_t next_process_id = 4u;
utils::DoublyLinkedList<UserProcess> process_list;

// The system exception handler
static utils::RefPtr<Dispatcher> system_exception_handler;
static mx_exception_behaviour_t system_exception_behaviour;
static mutex_t system_exception_mutex;

void magenta_init(uint level) {
    mutex_init(&handle_mutex);
    handle_arena.Init("handles", kMaxHandleCount);

    mutex_init(&process_mutex);

    system_exception_behaviour = MX_EXCEPTION_BEHAVIOUR_DEFAULT;
    mutex_init(&system_exception_mutex);
}

Handle* MakeHandle(utils::RefPtr<Dispatcher> dispatcher, mx_rights_t rights) {
    AutoLock lock(&handle_mutex);
    return handle_arena.New(utils::move(dispatcher), rights);
}

Handle* DupHandle(Handle* source) {
    AutoLock lock(&handle_mutex);
    return handle_arena.New(*source);
}

void DeleteHandle(Handle* handle) {
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

uint32_t AddProcess(UserProcess* process) {
    // Don't call any method of |process|, it is not yet fully constructed.
    AutoLock lock(&process_mutex);
    ++next_process_id;
    process_list.push_front(process);
    LTRACEF("Adding process %p : id = %u\n", process, next_process_id);
    return next_process_id;
}

void RemoveProcess(UserProcess* process) {
    AutoLock lock(&process_mutex);
    process_list.remove(process);
    LTRACEF("Removing process %p : id = %u\n", process, process->id());
}

void DebugDumpProcessList() {
    AutoLock lock(&process_mutex);
    printf("ps: [id] [#threads] [#handles] [addr] [name]\n");
    utils::for_each(&process_list, [](UserProcess* process) {
        printf("%3u %3u %3u %p %s\n",
            process->id(),
            process->ThreadCount(),
            process->HandleCount(),
            process,
            process->name().data());
    });
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
        return -1;
    }

    if (!strcmp(argv[1].str, "ps")) {
        DebugDumpProcessList();
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
