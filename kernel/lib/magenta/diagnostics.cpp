// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

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
    printf("#ev : number of event handles\n");
    printf("#ip : number of io port handles\n");
    printf("#dp : number of data pipe handles (both)\n");
    printf("#it : number of interrupt handles\n");
    printf("#io : number of io map handles\n");
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

uint32_t BuildHandleStats(const ProcessDispatcher& pd, uint32_t* handle_type, size_t size) {
    AutoLock lock(&pd.handle_table_lock_);
    uint32_t total = 0;
    for (const auto& handle : pd.handles_) {
        if (handle_type) {
            uint32_t type = static_cast<uint32_t>(handle.dispatcher()->GetType());
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

    snprintf(buf, sizeof(buf), "%3u: %3u %3u %3u %3u %3u %3u %3u %3u %3u",
             handle_count,
             types[1],              // process.
             types[2],              // thread.
             types[3],              // vmem
             types[4],              // msg pipe.
             types[5],              // event
             types[6],              // ioport.
             types[7] + types[8],   // data pipe (both sides),
             types[9],              // interrupt.
             types[10]              // io map
             );
    return buf;
}

void DumpProcessList() {
    AutoLock lock(& ProcessDispatcher::global_process_list_mutex_);
    printf("%8s-s  #t  #h:  #pr #th #vm #mp #ev #ip #dp #it #io[name]\n", "id");

    for (const auto& process : ProcessDispatcher::global_process_list_) {
        printf("%8llu-%c %3u %s [%s]\n",
               process.get_koid(),
               StateChar(process),
               process.ThreadCount(),
               DumpHandleTypeCount_NoLock(process),
               process.name().data());
    }
}

static int cmd_diagnostics(int argc, const cmd_args* argv) {
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
            DumpProcessList();
        }
    } else {
        printf("unrecognized subcommand\n");
        goto usage;
    }
    return rc;
}

STATIC_COMMAND_START
STATIC_COMMAND("mx", "magenta diagnostics", &cmd_diagnostics)
STATIC_COMMAND_END(mx);
