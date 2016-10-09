// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <kernel/auto_lock.h>
#include <lib/console.h>

#include <magenta/process_dispatcher.h>

void DumpProcessListKeyMap() {
    printf("id  : process id number\n");
    printf("-s  : state: R = running D = dead\n");
    printf("#t  : number of threads\n");
    printf("#h  : total number of handles\n");
    printf("#pr : number of process handles\n");
    printf("#th : number of thread handles\n");
    printf("#vm : number of vm map handles\n");
    printf("#mp : number of message pipe handles\n");
    printf("#ev : number of event and event pair handles\n");
    printf("#ip : number of io port handles\n");
    printf("#dp : number of data pipe handles (both)\n");
}

char StateChar(const ProcessDispatcher& pd) {
    switch (pd.state()) {
        case ProcessDispatcher::State::INITIAL:
            return 'I';
        case ProcessDispatcher::State::RUNNING:
            return 'R';
        case ProcessDispatcher::State::DYING:
            return 'Y';
        case ProcessDispatcher::State::DEAD:
            return 'D';
    }
    return '?';
}

const char* ObjectTypeToString(mx_obj_type_t type) {
    static_assert(MX_OBJ_TYPE_LAST == 18, "need to update switch below");

    switch (type) {
        case MX_OBJ_TYPE_PROCESS: return "process";
        case MX_OBJ_TYPE_THREAD: return "thread";
        case MX_OBJ_TYPE_VMEM: return "vmo";
        case MX_OBJ_TYPE_MESSAGE_PIPE: return "msg-pipe";
        case MX_OBJ_TYPE_EVENT: return "event";
        case MX_OBJ_TYPE_IOPORT: return "io-port";
        case MX_OBJ_TYPE_DATA_PIPE_PRODUCER: return "data-pipe-con";
        case MX_OBJ_TYPE_DATA_PIPE_CONSUMER: return "data-pipe-prod";
        case MX_OBJ_TYPE_INTERRUPT: return "interrupt";
        case MX_OBJ_TYPE_IOMAP: return "io-map";
        case MX_OBJ_TYPE_PCI_DEVICE: return "pci-device";
        case MX_OBJ_TYPE_LOG: return "log";
        case MX_OBJ_TYPE_WAIT_SET: return "wait-set";
        case MX_OBJ_TYPE_SOCKET: return "socket";
        case MX_OBJ_TYPE_RESOURCE: return "resource";
        case MX_OBJ_TYPE_EVENT_PAIR: return "event-pair";
        case MX_OBJ_TYPE_JOB: return "job";
        default: return "???";
    }
}

uint32_t BuildHandleStats(const ProcessDispatcher& pd, uint32_t* handle_type, size_t size) {
    AutoLock lock(&pd.handle_table_lock_);
    uint32_t total = 0;
    for (const auto& handle : pd.handles_) {
        if (handle_type) {
            uint32_t type = static_cast<uint32_t>(handle.dispatcher()->get_type());
            if (size > type)
                ++handle_type[type];
        }
        ++total;
    }
    return total;
}

uint32_t ProcessDispatcher::ThreadCount() const {
    AutoLock lock(&thread_list_lock_);
    return static_cast<uint32_t>(thread_list_.size_slow());
}

char* DumpHandleTypeCount_NoLock(const ProcessDispatcher& pd) {
    static char buf[(MX_OBJ_TYPE_LAST * 4) + 1];

    uint32_t types[MX_OBJ_TYPE_LAST] = {0};
    uint32_t handle_count = BuildHandleStats(pd, types, sizeof(types));

    snprintf(buf, sizeof(buf), "%3u: %3u %3u %3u %3u %3u %3u %3u",
             handle_count,
             types[MX_OBJ_TYPE_PROCESS],
             types[MX_OBJ_TYPE_THREAD],
             types[MX_OBJ_TYPE_VMEM],
             types[MX_OBJ_TYPE_MESSAGE_PIPE],
             // Events and event pairs:
             types[MX_OBJ_TYPE_EVENT] + types[MX_OBJ_TYPE_EVENT_PAIR],
             types[MX_OBJ_TYPE_IOPORT],
             // Both sides of data pipes:
             types[MX_OBJ_TYPE_DATA_PIPE_PRODUCER] + types[MX_OBJ_TYPE_DATA_PIPE_CONSUMER]
             );
    return buf;
}

void DumpProcessList() {
    AutoLock lock(& ProcessDispatcher::global_process_list_mutex_);
    printf("%8s-s  #t  #h:  #pr #th #vm #mp #ev #ip #dp #it #io[name]\n", "id");

    for (const auto& process : ProcessDispatcher::global_process_list_) {
        printf("%8" PRIu64 "-%c %3u %s [%s]\n",
               process.get_koid(),
               StateChar(process),
               process.ThreadCount(),
               DumpHandleTypeCount_NoLock(process),
               process.name().data());
    }
}

void DumpProcessHandles(mx_koid_t id) {
    mxtl::RefPtr<ProcessDispatcher> pd;
    {
        AutoLock lock(& ProcessDispatcher::global_process_list_mutex_);

        auto process_iter =
            ProcessDispatcher::global_process_list_.find_if([id] (const ProcessDispatcher& pd) {
                return (id == pd.get_koid());
        });

        if (!process_iter.IsValid()) {
            printf("process %" PRIu64 " not found\n", id);
            return;
        }
        pd.reset(process_iter.CopyPointer());
    }

    printf("process [%" PRIu64 "] handles :\n", id);
    printf("handle       koid : type\n");

    AutoLock lock(&pd->handle_table_lock_);
    uint32_t total = 0;
    for (const auto& handle : pd->handles_) {
        auto type = handle.dispatcher()->get_type();
        printf("%9d %7" PRIu64 " : %s\n",
            pd->MapHandleToValue(&handle),
            handle.dispatcher()->get_koid(),
            ObjectTypeToString(type));
        ++total;
    }
    printf("total: %u handles\n", total);
}

void KillProcess(mx_koid_t id) {
    // search the process list and send a kill if found
    mxtl::RefPtr<ProcessDispatcher> proc_ref;

    // search the process list for our process
    AutoLock lock(& ProcessDispatcher::global_process_list_mutex_);
    for (auto& process : ProcessDispatcher::global_process_list_) {
        if (process.get_koid() == id) {
            proc_ref.reset(&process);
            break;
        }
    }

    // if found, outside of the lock hit it with kill
    if (proc_ref) {
        printf("killing process %" PRIu64 "\n", id);
        proc_ref->Kill();
    }
}

static int cmd_diagnostics(int argc, const cmd_args* argv) {
    int rc = 0;

    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments:\n");
    usage:
        printf("%s ps         : list processes\n", argv[0].str);
        printf("%s ht   <pid> : dump process handles\n", argv[0].str);
        printf("%s kill <pid> : kill process\n", argv[0].str);
        return -1;
    }

    if (strcmp(argv[1].str, "ps") == 0) {
        if ((argc == 3) && (strcmp(argv[2].str, "help") == 0)) {
            DumpProcessListKeyMap();
        } else {
            DumpProcessList();
        }
    } else if (strcmp(argv[1].str, "ht") == 0) {
        if (argc < 3)
            goto usage;
        DumpProcessHandles(argv[2].u);
    } else if (strcmp(argv[1].str, "kill") == 0) {
        if (argc < 3)
            goto usage;
        KillProcess(argv[2].u);
    } else {
        printf("unrecognized subcommand\n");
        goto usage;
    }
    return rc;
}

STATIC_COMMAND_START
STATIC_COMMAND("mx", "magenta diagnostics", &cmd_diagnostics)
STATIC_COMMAND_END(mx);
