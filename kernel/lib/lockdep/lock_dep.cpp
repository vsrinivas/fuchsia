// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <debug.h>
#include <kernel/event.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <vm/vm.h>

#include <lib/console.h>
#include <lib/version.h>

#include <inttypes.h>
#include <string.h>

#include <fbl/atomic.h>
#include <fbl/new.h>
#include <lockdep/lockdep.h>

// Always assert to catch changes when lockdep is not enabled.
static_assert(sizeof(lockdep_state_t) == sizeof(lockdep::ThreadLockState),
              "lockdep_state_t and lockdep::ThreadLockState out of sync!");

#if WITH_LOCK_DEP

namespace {

// Event to wake up the loop detector thread when a new edge is added to the
// lock dependency graph.
event_t graph_edge_event =
    EVENT_INITIAL_VALUE(graph_edge_event, false, EVENT_FLAG_AUTOUNSIGNAL);

// Loop detection thread. Traverses the lock dependency graph to find circular
// lock dependencies.
int LockDepThread(void* /*arg*/) {
    while (true) {
        __UNUSED zx_status_t error = event_wait(&graph_edge_event);

        // Add some hysteresis to avoid re-triggering the loop detector on
        // close successive updates to the graph and to give the inline
        // validation reports a chance to print out first.
        thread_sleep_etc(ZX_SEC(2), /*interruptible=*/true);

        lockdep::LoopDetectionPass();
    }
    return 0;
}

void LockDepInit(unsigned /*level*/) {
    thread_t* t = thread_create("lockdep", &LockDepThread, NULL,
                                LOW_PRIORITY, DEFAULT_STACK_SIZE);
    thread_detach_and_resume(t);
}

// Dumps the state of the lock dependency graph.
void DumpLockClassState() {
    printf("Lock class states:\n");
    for (auto& state : lockdep::LockClassState::Iter()) {
        printf("  %s {\n", state.name());
        for (lockdep::LockClassId id : state.dependency_set()) {
            printf("    %s\n", lockdep::LockClassState::GetName(id));
        }
        printf("  }\n");
    }
    printf("\nConnected sets:\n");
    for (auto& state : lockdep::LockClassState::Iter()) {
        // Only handle root nodes in the outer loop. The nested loop will pick
        // up all of the child nodes under each parent node.
        if (state.connected_set() == &state) {
            printf("{\n");
            for (auto& other_state : lockdep::LockClassState::Iter()) {
                if (other_state.connected_set() == &state)
                    printf("  %s\n", other_state.name());
            }
            printf("}\n");
        }
    }
}

// Top-level lockdep command.
int CommandLockDep(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
        printf("Not enough arguments:\n");
    usage:
        printf("%s dump              : dump lock classes\n", argv[0].str);
        printf("%s loop              : trigger loop detection pass\n", argv[0].str);
        return -1;
    }

    if (strcmp(argv[1].str, "dump") == 0) {
        DumpLockClassState();
    } else if (strcmp(argv[1].str, "loop") == 0) {
        printf("Triggering loop detection pass:\n");
        lockdep::SystemTriggerLoopDetection();
    } else {
        printf("Unrecognized subcommand: '%s'\n", argv[1].str);
        goto usage;
    }

    return 0;
}

// Utility to cast from lockdep_state_t* to ThreadLockState*.
inline lockdep::ThreadLockState* ToThreadLockState(lockdep_state_t* state) {
    return reinterpret_cast<lockdep::ThreadLockState*>(state);
}

} // anonymous namespace

STATIC_COMMAND_START
STATIC_COMMAND("lockdep", "kernel lock diagnostics", &CommandLockDep)
STATIC_COMMAND_END(lockdep);

LK_INIT_HOOK(lockdep, LockDepInit, LK_INIT_LEVEL_THREADING);

namespace lockdep {

// Prints a kernel oops when a normal lock order violation is detected.
void SystemLockValidationError(AcquiredLockEntry* bad_entry,
                               AcquiredLockEntry* conflicting_entry,
                               ThreadLockState* state,
                               void* caller_address,
                               void* caller_frame,
                               LockResult result) {
    thread_t* const current_thread = get_current_thread();

    char owner_name[THREAD_NAME_LENGTH];
    thread_owner_name(current_thread, owner_name);

    const uint64_t user_pid = current_thread->user_pid;
    const uint64_t user_tid = current_thread->user_tid;

    printf("\nZIRCON KERNEL PANIC\n");
    printf("Lock validation failed for thread %p pid %" PRIu64 " tid %" PRIu64 " (%s:%s):\n",
           current_thread, user_pid, user_tid, owner_name, current_thread->name);
    printf("Reason: %s\n", ToString(result));
    printf("Bad lock: name=%s order=%" PRIu64 "\n",
           LockClassState::GetName(bad_entry->id()),
           bad_entry->order());
    printf("Conflict: name=%s order=%" PRIu64 "\n",
           LockClassState::GetName(conflicting_entry->id()),
           conflicting_entry->order());
    printf("caller=%p frame=%p\n", caller_address, caller_frame);
    printf("BUILDID %s\n", version.buildid);

    // Log the ELF build ID in the format the symbolizer scripts understand.
    if (version.elf_build_id[0] != '\0') {
        printf("dso: id=%s base=%#lx name=zircon.elf\n",
               version.elf_build_id, (uintptr_t)__code_start);
    }

    thread_print_current_backtrace_at_frame(caller_frame);
    printf("\n");
}

// Issues a kernel panic when a fatal lock order violation is detected.
void SystemLockValidationFatal(AcquiredLockEntry* lock_entry,
                               ThreadLockState* state,
                               void* caller_address,
                               void* caller_frame,
                               LockResult result) {
    _panic(caller_address, caller_frame,
           "Fatal lock violation detected! name=%s reason=%s\n",
           LockClassState::GetName(lock_entry->id()), ToString(result));
}

// Prints a kernel oops when a circular lock dependency is detected.
void SystemCircularLockDependencyDetected(LockClassState* connected_set_root) {
    printf("\nZIRCON KERNEL OOPS\n");
    printf("Circular lock dependency detected:\n");

    for (auto& node : lockdep::LockClassState::Iter()) {
        if (node.connected_set() == connected_set_root)
            printf("  %s\n", node.name());
    }

    printf("\n");
}

// Returns a pointer to the ThreadLockState instance for the current thread when
// thread context or for the current CPU when in irq context.
ThreadLockState* SystemGetThreadLockState() {
    ThreadLockState* state;

    if (arch_in_int_handler())
        state = ToThreadLockState(&get_local_percpu()->lock_state);
    else
        state = ToThreadLockState(&get_current_thread()->lock_state);

    return state;
}

// Initializes an instance of ThreadLockState.
void SystemInitThreadLockState(ThreadLockState* state) {
    new (state) ThreadLockState();
}

// Wakes up the loop detector thread to re-evaluate the depedency graph.
void SystemTriggerLoopDetection() {
    event_signal(&graph_edge_event, /*reschedule=*/false);
}

} // namespace lockdep

#endif
