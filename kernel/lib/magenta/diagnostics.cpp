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

#include <magenta/job_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>

static void DumpProcessListKeyMap() {
    printf("id  : process id number\n");
    printf("-s  : state: R = running D = dead\n");
    printf("#t  : number of threads\n");
    printf("#pg : number of allocated pages\n");
    printf("#h  : total number of handles\n");
    printf("#jb : number of job handles\n");
    printf("#pr : number of process handles\n");
    printf("#th : number of thread handles\n");
    printf("#vo : number of vmo handles\n");
    printf("#vm : number of virtual memory address region handles\n");
    printf("#ch : number of channel handles\n");
    printf("#ev : number of event and event pair handles\n");
    printf("#ip : number of io port handles\n");
}

static char StateChar(const ProcessDispatcher& pd) {
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

static const char* ObjectTypeToString(mx_obj_type_t type) {
    static_assert(MX_OBJ_TYPE_LAST == 19, "need to update switch below");

    switch (type) {
        case MX_OBJ_TYPE_PROCESS: return "process";
        case MX_OBJ_TYPE_THREAD: return "thread";
        case MX_OBJ_TYPE_VMEM: return "vmo";
        case MX_OBJ_TYPE_CHANNEL: return "channel";
        case MX_OBJ_TYPE_EVENT: return "event";
        case MX_OBJ_TYPE_IOPORT: return "io-port";
        case MX_OBJ_TYPE_INTERRUPT: return "interrupt";
        case MX_OBJ_TYPE_IOMAP: return "io-map";
        case MX_OBJ_TYPE_PCI_DEVICE: return "pci-device";
        case MX_OBJ_TYPE_LOG: return "log";
        case MX_OBJ_TYPE_WAIT_SET: return "wait-set";
        case MX_OBJ_TYPE_SOCKET: return "socket";
        case MX_OBJ_TYPE_RESOURCE: return "resource";
        case MX_OBJ_TYPE_EVENT_PAIR: return "event-pair";
        case MX_OBJ_TYPE_JOB: return "job";
        case MX_OBJ_TYPE_VMAR: return "vmar";
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

size_t ProcessDispatcher::PageCount() const {
    return aspace_->AllocatedPages();
}

static char* DumpHandleTypeCount_NoLock(const ProcessDispatcher& pd) {
    static char buf[(MX_OBJ_TYPE_LAST * 4) + 1];

    uint32_t types[MX_OBJ_TYPE_LAST] = {0};
    uint32_t handle_count = BuildHandleStats(pd, types, sizeof(types));

    snprintf(buf, sizeof(buf), "%3u: %3u %3u %3u %3u %3u %3u %3u %3u",
             handle_count,
             types[MX_OBJ_TYPE_JOB],
             types[MX_OBJ_TYPE_PROCESS],
             types[MX_OBJ_TYPE_THREAD],
             types[MX_OBJ_TYPE_VMEM],
             types[MX_OBJ_TYPE_VMAR],
             types[MX_OBJ_TYPE_CHANNEL],
             // Events and event pairs:
             types[MX_OBJ_TYPE_EVENT] + types[MX_OBJ_TYPE_EVENT_PAIR],
             types[MX_OBJ_TYPE_IOPORT]
             );
    return buf;
}

void DumpProcessList() {
    AutoLock lock(& ProcessDispatcher::global_process_list_mutex_);
    printf("%8s-s  #t  #pg  #h:  #jb #pr #th #vo #vm #ch #ev #ip #dp [  job:name]\n", "id");

    for (const auto& process : ProcessDispatcher::global_process_list_) {
        char pname[MX_MAX_NAME_LEN];
        process.get_name(pname);
        printf("%8" PRIu64 "-%c %3u %4zu %s  [%5" PRIu64 ":%s]\n",
               process.get_koid(),
               StateChar(process),
               process.ThreadCount(),
               process.PageCount(),
               DumpHandleTypeCount_NoLock(process),
               process.get_inner_koid(),
               pname);
    }
}

void DumpProcessHandles(mx_koid_t id) {
    auto pd = ProcessDispatcher::LookupProcessById(id);
    if (!pd) {
        printf("process not found!\n");
        return;
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


class JobDumper final : public JobEnumerator {
public:
    JobDumper(mx_koid_t self) : self_(self) {}
    JobDumper(const JobDumper&) = delete;

private:
    bool Size(uint32_t proc_count, uint32_t job_count) final {
        if (!job_count)
            printf("no jobs\n");
        if (proc_count < 2)
            printf("no processes\n");
        return true;
    }

    bool OnJob(JobDispatcher* job, uint32_t index) final {
        printf("- %" PRIu64 " child job (%" PRIu32 " processes)\n",
            job->get_koid(), job->process_count());
        return true;
    }

    bool OnProcess(ProcessDispatcher* proc, uint32_t index) final {
        auto id = proc->get_koid();
        if (id != self_) {
            char pname[MX_MAX_NAME_LEN];
            proc->get_name(pname);
            printf("- %" PRIu64 " proc [%s]\n", id, pname);
        }

        return true;
    }

    mx_koid_t self_;
};

void DumpJobTreeForProcess(mx_koid_t id) {
    auto pd = ProcessDispatcher::LookupProcessById(id);
    if (!pd) {
        printf("process not found!\n");
        return;
    }

    auto job = pd->job();
    if (!job) {
        printf("process has no job!!\n");
        return;
    }

    char pname[MX_MAX_NAME_LEN];
    pd->get_name(pname);

    printf("process %" PRIu64 " [%s]\n", id, pname);
    printf("in job [%" PRIu64 "]", job->get_koid());

    auto parent = job;
    while (true) {
        parent = parent->parent();
        if (!parent)
            break;
        printf("-->[%" PRIu64 "]", parent->get_koid());
    }
    printf("\n");

    JobDumper dumper(id);
    job->EnumerateChildren(&dumper);
}

void KillProcess(mx_koid_t id) {
    // search the process list and send a kill if found
    auto pd = ProcessDispatcher::LookupProcessById(id);
    if (!pd) {
        printf("process not found!\n");
        return;
    }
    // if found, outside of the lock hit it with kill
    printf("killing process %" PRIu64 "\n", id);
    pd->Kill();
}

void DumpProcessAddressSpace(mx_koid_t id) {
    auto pd = ProcessDispatcher::LookupProcessById(id);
    if (!pd) {
        printf("process not found!\n");
        return;
    }

    pd->aspace()->Dump();
}

static size_t mwd_limit = 32 * 256;
static bool mwd_running;

void DumpProcessMemoryUsage(const char* prefix, size_t limit) {
    AutoLock lock(& ProcessDispatcher::global_process_list_mutex_);

    for (const auto& process : ProcessDispatcher::global_process_list_) {
        size_t pages = process.PageCount();
        if (pages > limit) {
            char pname[MX_MAX_NAME_LEN];
            process.get_name(pname);
            printf("%s%s: %zu MB\n", prefix, pname, pages / 256);
        }
    }
}

static int mwd_thread(void* arg) {
    for (;;) {
        thread_sleep(1000);
        DumpProcessMemoryUsage("MemoryHog! ", mwd_limit);
    }
}

static int cmd_diagnostics(int argc, const cmd_args* argv) {
    int rc = 0;

    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments:\n");
    usage:
        printf("%s ps         : list processes\n", argv[0].str);
        printf("%s mwd  <mb>  : memory watchdog\n", argv[0].str);
        printf("%s ht   <pid> : dump process handles\n", argv[0].str);
        printf("%s jb   <pid> : list job tree\n", argv[0].str);
        printf("%s kill <pid> : kill process\n", argv[0].str);
        printf("%s asd  <pid> : dump process address space\n", argv[0].str);
        return -1;
    }

    if (strcmp(argv[1].str, "mwd") == 0) {
        if (argc == 3) {
            mwd_limit = argv[2].u * 256;
        }
        if (!mwd_running) {
            thread_t* t = thread_create("mwd", mwd_thread, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
            if (t) {
                mwd_running = true;
                thread_resume(t);
            }
        }
    } else if (strcmp(argv[1].str, "ps") == 0) {
        if ((argc == 3) && (strcmp(argv[2].str, "help") == 0)) {
            DumpProcessListKeyMap();
        } else {
            DumpProcessList();
        }
    } else if (strcmp(argv[1].str, "ht") == 0) {
        if (argc < 3)
            goto usage;
        DumpProcessHandles(argv[2].u);
    } else if (strcmp(argv[1].str, "jb") == 0) {
        if (argc < 3)
            goto usage;
        DumpJobTreeForProcess(argv[2].u);
    } else if (strcmp(argv[1].str, "kill") == 0) {
        if (argc < 3)
            goto usage;
        KillProcess(argv[2].u);
    } else if (strcmp(argv[1].str, "asd") == 0) {
        if (argc < 3)
            goto usage;
        DumpProcessAddressSpace(argv[2].u);
    } else {
        printf("unrecognized subcommand\n");
        goto usage;
    }
    return rc;
}

STATIC_COMMAND_START
STATIC_COMMAND("mx", "magenta diagnostics", &cmd_diagnostics)
STATIC_COMMAND_END(mx);
