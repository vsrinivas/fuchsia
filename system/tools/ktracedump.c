// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/ktrace.h>

#define KTRACE_DEF(num,type,name,group) EVT_##name = num,
enum {
#include <magenta/ktrace-def.h>
};

// USE_NS 1 means pass time as 000.000 (microseconds) to traceview
//          internal timestamps in nanoseconds

// USE_NS 0 means pass time as 000 (microsecons) to traceview (less precise)
//          internal timestamps in microseconds

#define USE_NS 1

// define a 1 microsecond constant for internal timestamps
#if USE_NS
#define TS1 1000
#else
#define TS1 1
#endif

#define FNV32_PRIME (16777619)
#define FNV32_OFFSET_BASIS (2166136261)

// for bits 0..15
static inline uint32_t fnv1a_tiny(uint32_t n, uint32_t bits) {
    uint32_t hash = FNV32_OFFSET_BASIS;
    hash = (hash ^ (n & 0xFF)) * FNV32_PRIME; n >>= 8;
    hash = (hash ^ (n & 0xFF)) * FNV32_PRIME; n >>= 8;
    hash = (hash ^ (n & 0xFF)) * FNV32_PRIME; n >>= 8;
    hash = (hash ^ n) * FNV32_PRIME;
    return ((hash >> bits) ^ hash) & ((1 << bits) - 1);
}

typedef struct objinfo objinfo_t;
struct objinfo {
    objinfo_t* next;
    uint32_t id;
    uint32_t kind;
    uint32_t flags;
    uint32_t creator;
    uint32_t extra;
    uint32_t seq_src;
    uint32_t seq_dst;
    uint32_t last_state;
    uint64_t last_ts;
};

#define F_DEAD 1
#define F_INVISIBLE 2
#define F_RUNNING 4

#define HASHBITS 10
#define BUCKETS (1 << HASHBITS)

#define OBJBUCKET(id) fnv1a_tiny(id, HASHBITS)
objinfo_t *objhash[BUCKETS];

#define KPROC    1 // extra = 0
#define KTHREAD  2 // extra = pid
#define KPIPE    3 // extra = other-pipe-id
#define KPORT    4 // extra = 0

const char* kind_string(uint32_t kind) {
    switch (kind) {
    case KPROC:   return "PROC";
    case KTHREAD: return "THRD";
    case KPIPE:   return "MPIP";
    case KPORT:   return "PORT";
    default:      return "NVLD";
    }
}

objinfo_t* find_object(uint32_t id, uint32_t kind) {
    for (objinfo_t* oi = objhash[OBJBUCKET(id)]; oi != NULL; oi = oi->next) {
        if (oi->id == id) {
            if (kind && (oi->kind != kind)) {
                fprintf(stderr, "error: object(%08x) kind %d != %d\n", id, kind, oi->kind);
            }
            return oi;
        }
    }
    return NULL;
}

objinfo_t* new_object(uint32_t id, uint32_t kind, uint32_t creator, uint32_t extra) {
    if (find_object(id, 0) != NULL) {
        fprintf(stderr, "error: object(%08x) already exists!\n", id);
    }
    objinfo_t* oi = calloc(1, sizeof(objinfo_t));
    oi->id = id;
    oi->kind = kind;
    oi->creator = creator;
    oi->extra = extra;
    unsigned n = OBJBUCKET(id);
    oi->next = objhash[n];
    objhash[n] = oi;
    return oi;
}

int is_object(uint32_t id, uint32_t flags) {
    objinfo_t* oi = find_object(id, 0);
    if (oi && (oi->flags & flags)) {
        return 1;
    } else {
        return 0;
    }
}

void for_each_object(void (*func)(objinfo_t* oi, uint64_t ts), uint64_t ts) {
    for (unsigned n = 0; n < BUCKETS; n++) {
        for (objinfo_t* oi = objhash[n]; oi != NULL; oi = oi->next) {
            func(oi, ts);
        }
    }
}

void evt_thread_name(uint32_t pid, uint32_t tid, const char* name);

// kthread ids are their kvaddrs and may collide with koids
// but there are usually only a handful of these, so just use
// a simple linked list
typedef struct kthread kthread_t;
struct kthread {
    kthread_t* next;
    uint64_t last_ts;
    uint32_t id;
};

kthread_t* kthread_list;

kthread_t* find_kthread(uint32_t id) {
    kthread_t* t;
    for (t = kthread_list; t != NULL; t = t->next) {
        if (t->id == id) {
            return t;
        }
    }
    t = malloc(sizeof(kthread_t));
    t->id = id;
    t->last_ts = 0;
    t->next = kthread_list;
    kthread_list = t;
    evt_thread_name(0, id, (id & 0x80000000) ? "idle" : "kernel");
    return t;
}


static uint64_t ticks_per_ms;

uint64_t ticks_to_ts(uint64_t ts) {
    //TODO: handle overflow for large times
    if (ticks_per_ms) {
#if USE_NS
        return (ts * 1000000UL) / ticks_per_ms;
#else
        return (ts * 1000UL) / ticks_per_ms;
#endif
    } else {
        return 0;
    }
}

const char* recname(ktrace_rec_name_t* rec) {
    uint32_t len = KTRACE_LEN(rec->tag);
    if (len <= (KTRACE_NAMESIZE + 1)) {
        return "ERROR";
    }
    len -= (KTRACE_NAMESIZE + 1);
    rec->name[len] = 0;
    return rec->name;
}

uint32_t other_pipe(uint32_t id) {
    objinfo_t* oi = find_object(id, KPIPE);
    if (oi) {
        return oi->extra;
    } else {
        fprintf(stderr, "error: pipe object(%08x) missing\n", id);
        return 0;
    }
}

uint32_t thread_to_process(uint32_t id) {
    objinfo_t* oi = find_object(id, KTHREAD);
    if (oi) {
        return oi->extra;
    } else {
        return 0;
    }
}

int verbose = 0;
int json = 0;
int with_kthreads = 0;
int with_msgpipe_io = 0;
int with_waiting = 0;
int with_syscalls = 0;

typedef struct evtinfo {
    uint64_t ts;
    uint32_t pid;
    uint32_t tid;
} evt_info_t;

#define trace(fmt...) do { if(!json) printf(fmt); } while (0)

void trace_hdr(evt_info_t* ei, uint32_t tag) {
    if (json) {
        return;
    }
#if USE_NS
    printf("%04lu.%09lu [%08x] ",
           ei->ts/(1000000000UL), ei->ts%(1000000000UL), ei->tid);
#else
    printf("%04lu.%06lu [%08x] ",
           ei->ts/(1000000UL), ei->ts%(1000000UL), ei->tid);
#endif
}

char* _json(char* out, const char* name, va_list ap) {
    const char* str;
    uint32_t val32;
    uint64_t val64;
    int depth = 0;
    int comma = 0;
    for (;;) {
        if (name == NULL) {
            break;
        }
        if (comma && (name[0] != '}')) {
            *out++ = ',';
        }
        switch (name[0]) {
        case '#':
            val32 = va_arg(ap, uint32_t);
            out += sprintf(out, "\"%s\":%u", name + 1, val32);
            comma = 1;
            break;
        case '@':
            val64 = va_arg(ap, uint64_t);
            if (val64 == 0) {
                fprintf(stderr, "error: duration 0 (dropping event)\n");
                return NULL;
            }
#if USE_NS
            out += sprintf(out, "\"%s\":%lu.%03lu", name + 1, val64 / 1000UL, val64 % 1000UL);
#else
            out += sprintf(out, "\"%s\":%lu", name + 1, val64);
#endif
            comma = 1;
            break;
        case '{':
            out += sprintf(out, "\"%s\":{", name + 1);
            depth++;
            comma = 0;
            break;
        case '}':
            if (depth == 0) {
                fprintf(stderr, "fatal: unbalanced json\n");
                exit(-1);
            }
            *out++ = '}';
            depth--;
            comma = 1;
            break;
        default:
            str = va_arg(ap, const char*);
            out += sprintf(out, "\"%s\":\"%s\"", name, str);
            comma = 1;
            break;
        }
        name = va_arg(ap, const char*);
    }
    *out = 0;
    if (depth > 0) {
        fprintf(stderr, "fatal: unbalanced json\n");
        exit(-1);
    }
    return out;
}

void json_obj(const char* name0, ...) {
    if (json) {
        char obj[1024];
        char *out;
        obj[0] = '{';
        va_list ap;
        va_start(ap, name0);
        if ((out = _json(obj + 1, name0, ap)) == NULL) {
            return;
        }
        va_end(ap);
        out += sprintf(out, "},\n");
        fwrite(obj, 1, out - obj, stdout);
    }
}

void json_rec(uint64_t ts, const char* phase, const char* name, const char* cat,
              const char* name0, ...) {
    if (json) {
        char obj[1024];
        char *out;
#if USE_NS
        out = obj + sprintf(obj, "{\"ts\":%lu.%03lu,\"ph\":\"%s\",\"name\":\"%s\",\"cat\":\"%s\",",
                            ts / 1000UL, ts % 1000UL, phase, name, cat);
#else
        out = obj + sprintf(obj, "{\"ts\":%lu,\"ph\":\"%s\",\"name\":\"%s\",\"cat\":\"%s\",",
                            ts, phase, name, cat);
#endif
        va_list ap;
        va_start(ap, name0);
        out = _json(out, name0, ap);
        va_end(ap);
        if (out == NULL) {
            return;
        }
        out += sprintf(out, "},\n");
        fwrite(obj, 1, out - obj, stdout);
    }
}

enum thread_state {
    THREAD_SUSPENDED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_DEATH,
};
const char* thread_state_msg(uint32_t state) {
    switch (state) {
    case THREAD_SUSPENDED: return "suspended";
    case THREAD_READY: return "ready";
    case THREAD_RUNNING: return "running";
    case THREAD_BLOCKED: return "waiting";
    case THREAD_SLEEPING: return "";
    case THREAD_DEATH: return "dead";
    default: return "unknown";
    }
}
const char* thread_state_color(uint32_t state) {
    switch (state) {
    case THREAD_SUSPENDED: return "thread_state_runnable";
    case THREAD_READY: return "thread_state_runnable";
    case THREAD_RUNNING: return "thread_state_running";
    case THREAD_BLOCKED: return "thread_state_unknown";
    case THREAD_SLEEPING: return "thread_state_sleeping";
    case THREAD_DEATH: return "thread_state_iowait";
    default: return "unknown";
    }
}

void evt_context_switch(evt_info_t* ei, uint32_t newpid, uint32_t newtid,
                        uint32_t state, uint32_t cpu, uint32_t oldthread, uint32_t newthread) {
    char name[32];
    sprintf(name, "cpu%u", cpu);

    // for both user and kernel threads we emit a complete event (start + duration)
    // if the old thread has a recorded start timestamp, and we record a start
    // timestamp in the new thread that we're switching to.
    if (with_kthreads) {
        if (ei->tid == 0) {
            kthread_t* t = find_kthread(oldthread);
            if (t->last_ts) {
                json_rec(t->last_ts, "X", name, name,
                         "@dur", ei->ts - t->last_ts,
                         "#pid", 0,
                         "#tid", oldthread, NULL);
                t->last_ts = 0;
            }
        }
        if (newtid == 0) {
            kthread_t* t = find_kthread(newthread);
            if (t->last_ts) {
                fprintf(stderr, "error: kthread %x already running?!\n", newthread);
            }
            t->last_ts = ei->ts;
        }
    }
    if (ei->pid && ei->tid && !is_object(ei->pid, F_INVISIBLE)) {
        objinfo_t* oi = find_object(ei->tid, KTHREAD);
        if (oi) {
            // we're going to sleep, record our wake time
            if (oi->last_ts) {
                json_rec(oi->last_ts, "X", name, "thread",
                         "@dur", ei->ts - oi->last_ts,
                         "cname", "thread_state_running",
                         "#pid", ei->pid,
                         "#tid", ei->tid, NULL);
            }
            oi->last_state = state;
            oi->last_ts = ei->ts;
            oi->flags &= (~F_RUNNING);
        }
    }
    if (newpid && newtid && !is_object(newpid, F_INVISIBLE)) {
        objinfo_t* oi = find_object(newtid, KTHREAD);
        if (oi) {
            // we just woke up, record our sleep time
            if (oi->last_ts) {
                json_rec(oi->last_ts, "X", thread_state_msg(oi->last_state), "thread",
                         "@dur", ei->ts - oi->last_ts,
                         "cname", thread_state_color(oi->last_state),
                         "#pid", newpid,
                         "#tid", newtid, NULL);
            }
            oi->last_ts = ei->ts;
            oi->flags = F_RUNNING;
        }
    }
}

void end_of_trace(objinfo_t* oi, uint64_t ts) {
    if (oi->kind == KTHREAD) {
        if (is_object(oi->extra, F_INVISIBLE)) {
            return;
        }
        if (oi->flags & F_RUNNING) {
            // simulate context switch away
            json_rec(oi->last_ts, "X", "cpu", "thread",
                     "@dur", ts - oi->last_ts,
                     "cname", "thread_state_running",
                     "#pid", oi->extra,
                     "#tid", oi->id, NULL);
        } else {
            // simulate context switch back
            json_rec(oi->last_ts, "X", thread_state_msg(oi->last_state), "thread",
                     "@dur", (oi->last_state == THREAD_DEATH) ? 10000 : (ts - oi->last_ts),
                     "cname", thread_state_color(oi->last_state),
                     "#pid", oi->extra,
                     "#tid", oi->id, NULL);
        }
    }
}

#define SYSCALL(args...) \
do { if (with_syscalls && ei->pid && ei->tid) { \
    char namestr[64]; \
    sprintf(namestr, "syscalls (%u)", ei->tid); \
    json_rec(ei->ts, "O", namestr, "syscall", \
             "#id", ei->tid, \
             "scope", "syscall", \
             "{args", "{snapshot", args, "}", "}", \
             "#pid", ei->pid, \
             "#tid", ei->tid, NULL); \
} } while (0)


uint32_t uniq = 1;

void evt_process_create(evt_info_t* ei, uint32_t pid) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    SYSCALL("op", "process_create()", "#pid", pid);
}
void evt_process_delete(evt_info_t* ei, uint32_t pid) {
}
void evt_process_start(evt_info_t* ei, uint32_t pid, uint32_t tid) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    SYSCALL("op", "process_start()", "#pid", pid, "#tid", tid);
}
void evt_process_name(uint32_t pid, const char* name, uint32_t index) {
    if (is_object(pid, F_INVISIBLE)) {
        return;
    }
    json_obj("ph", "M",
             "name", "process_name",
             "#pid", pid,
             "{args",
             "name", name,
             "}", NULL);
    json_obj("ph", "M",
             "name", "process_sort_index",
             "#pid", pid,
             "{args",
             "#sort_index", index,
             "}", NULL);
}

void evt_thread_create(evt_info_t* ei, uint32_t tid, uint32_t pid) {
}
void evt_thread_delete(evt_info_t* ei, uint32_t tid) {
#if 0 // not reliable, deletion != exit
    uint32_t pid = thread_to_process(tid);
    if (pid == 0) return;
    json_rec(ei->ts, "X", "exit", "thread",
             "#dur", 100,
             "cname", "thread_state_iowait",
             "#pid", pid,
             "#tid", tid, NULL);
#endif
}
void evt_thread_start(evt_info_t* ei, uint32_t tid) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    SYSCALL("op", "thread_start()", "#tid", tid);
}
void evt_thread_name(uint32_t pid, uint32_t tid, const char* name) {
    char tmp[128];
    sprintf(tmp, "%s (%u)", name, tid);
    json_obj("ph", "M",
             "name", "thread_name",
             "#pid", pid,
             "#tid", tid,
             "{args",
             "name", tmp,
             "}", NULL);
    if (pid == 0) {
        return;
    }
    if (!with_msgpipe_io) {
        return;
    }
    // create a parallel track for io flow markup
    sprintf(tmp, "%s-io (%u)", name, tid);
    char tmp2[64];
    sprintf(tmp2, "io:%u", tid);
    json_obj("ph", "M",
             "name", "thread_name",
             "#pid", pid,
             "tid", tmp2,
             "{args",
             "name", tmp,
             "}", NULL);
}

void evt_msgpipe_create(evt_info_t* ei, uint32_t id, uint32_t otherid) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    SYSCALL("op", "msgpipe_create()", "#id0", id, "#id1", otherid);
}

void evt_msgpipe_delete(evt_info_t* ei, uint32_t id) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    SYSCALL("op", "msgpipe_delete()", "#mpid", id);
}
void evt_msgpipe_write(evt_info_t* ei, uint32_t id, uint32_t otherid,
                       uint32_t bytes, uint32_t handles) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    if (ei->pid == 0) {
        // ignore writes from unknown threads
        return;
    }
    SYSCALL("op", "msgpipe_write()", "#mpid", id, "#otherid", otherid,
            "#bytes", bytes, "#handles", handles);

    if (!with_msgpipe_io) {
        return;
    }
    char tidstr[64];
    sprintf(tidstr, "io:%u", ei->tid);

    // duration 1 event to mark the write
    // instant events frustratingly vanish when you zoom in!
    json_rec(ei->ts, "X", "msg-read", "object",
             "@dur", TS1,
             "cname", "good",
             "#pid", ei->pid,
             "tid", tidstr,
             "{args",
             "func", "msgpipe_write()",
             "#bytes", bytes,
             "#handles", handles,
             "}", NULL);

    // if we can find the other half, start a flow event from
    // here to there
    objinfo_t* oi = find_object(id, KPIPE);
    if (oi == NULL) return;
    char xid[128];
    sprintf(xid, "%x:%x:%x", id, otherid, oi->seq_src++);
    json_rec(ei->ts, "s", "write", "msgpipe",
             "id", xid,
             "#pid", ei->pid,
             "tid", tidstr,
             NULL);
}
void evt_msgpipe_read(evt_info_t* ei, uint32_t id, uint32_t otherid,
                      uint32_t bytes, uint32_t handles) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    if (ei->pid == 0) {
        // ignore reads from unknown threads
        return;
    }

    SYSCALL("op", "msgpipe_read()", "#mpid", id, "#otherid", otherid,
            "#bytes", bytes, "#handles", handles);

    if (!with_msgpipe_io) {
        return;
    }

    char tidstr[64];
    sprintf(tidstr, "io:%u", ei->tid);
    // duration 1 event to mark the read
    // instant events frustratingly vanish when you zoom in!
    json_rec(ei->ts, "X", "msg-read", "object",
             "@dur", TS1,
             "cname", "good",
             "#pid", ei->pid,
             "tid", tidstr,
             "{args",
             "call", "msgpipe_read()",
             "#bytes", bytes,
             "#handles", handles,
             "}", NULL);

    // if we can find the other half, finish a flow event
    // from there to here
    objinfo_t* oi = find_object(otherid, KPIPE);
    if (oi == NULL) return;
    char xid[128];
    sprintf(xid, "%x:%x:%x", otherid, id, oi->seq_dst++);
    json_rec(ei->ts, "f", "read", "msgpipe",
             "bp", "e",
             "id", xid,
             "#pid", ei->pid,
             "tid", tidstr,
             NULL);
}

void evt_port_create(evt_info_t* ei, uint32_t id) {
}
void evt_port_wait(evt_info_t* ei, uint32_t id) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    SYSCALL("op", "port_wait()", "#portid", id);

    if (!with_waiting) {
        return;
    }
    char tidstr[64];
    sprintf(tidstr, "io:%u", ei->tid);
    json_rec(ei->ts, "i", "wait-port", "port",
             "@dur", TS1,
             "cname", "thread_state_iowait",
             "#pid", ei->pid,
             "tid", tidstr,
             NULL);
}
void evt_port_wait_done(evt_info_t* ei, uint32_t id) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    SYSCALL("op", "port_wait() done", "#portid", id);
}

void evt_port_delete(evt_info_t* ei, uint32_t id) {
}

void evt_wait_one(evt_info_t* ei, uint32_t id, uint32_t signals, uint64_t timeout) {
    if (is_object(ei->pid, F_INVISIBLE)) {
        return;
    }
    SYSCALL("op", "wait_one()", "#oid", id);

    if (!with_waiting) {
        return;
    }
    char tidstr[64];
    sprintf(tidstr, "io:%u", ei->tid);
    json_rec(ei->ts, "i", "wait-object", "object",
             "@dur", TS1,
             "cname", "thread_state_iowait",
             "#pid", ei->pid,
             "tid", tidstr,
             NULL);
}
void evt_wait_one_done(evt_info_t* ei, uint32_t id, uint32_t pending, uint32_t status) {
    SYSCALL("op", "wait_one() done", "#oid", id, "#pending", pending, "#status", status);
}

int usage(void) {
    fprintf(stderr,
            "usage: ktracedump [ <option> ]* <tracefile>\n\n"
            "option: -text        plain text output\n"
            "        -json        chrome://tracing output (default)\n"
            "        -limit=n     stop after n events\n"
            "        -msgpipe-io  show msgpipe read/write w/ flow\n"
            "        -kthreads    show kernel threads too\n"
            "        -wait-io     show waiting in msgpipe flow tracks\n"
            "        -syscalls    show syscall timelines\n"
            "        -all         enable all tracing features\n"
            "        -stats       print summary of trace at end\n"
            "        -onlypid=... only display pid(s) listed (comma separated)\n"
            );
    return -1;
}


typedef struct {
    uint64_t ts_first;
    uint64_t ts_last;
    uint32_t events;
    uint32_t context_switch;
    uint32_t msgpipe_new;
    uint32_t msgpipe_del;
    uint32_t msgpipe_write;
    uint32_t msgpipe_read;
    uint32_t thread_new;
    uint32_t thread_del;
    uint32_t process_new;
    uint32_t process_del;
} stats_t;

void dump_stats(stats_t* s) {
    fprintf(stderr, "-----------------------------------------\n");
    uint64_t duration = s->ts_last - s->ts_first;
    fprintf(stderr, "elapsed time:     %lu.%06lu s\n",
            duration / 1000000UL, duration % 1000000UL);
    fprintf(stderr, "total events:     %u\n", s->events);
    fprintf(stderr, "context switches: %u\n", s->context_switch);
    fprintf(stderr, "msgpipe created:  %u\n", s->msgpipe_new);
    fprintf(stderr, "msgpipe deleted:  %u\n", s->msgpipe_del);
    fprintf(stderr, "msgpipe writes:   %u\n", s->msgpipe_write);
    fprintf(stderr, "msgpipe reads:    %u\n", s->msgpipe_read);
    fprintf(stderr, "thread created:   %u\n", s->thread_new);
    fprintf(stderr, "process created:  %u\n", s->process_new);
}

#define MAX_VIS_PIDS 64
uint32_t visible_pids[MAX_VIS_PIDS];
uint32_t visible_count;

typedef union {
    ktrace_header_t hdr;
    ktrace_rec_32b_t x4;
    ktrace_rec_name_t name;
    uint8_t raw[256];
} ktrace_record_t;

int main(int argc, char** argv) {
    int show_stats = 0;
    stats_t s;
    ktrace_record_t rec;
    objinfo_t* oi;
    objinfo_t* oi2;
    unsigned offset = 0;
    unsigned limit = 0xFFFFFFFF;
    uint64_t t;
    uint32_t n;

    memset(&s, 0, sizeof(s));

    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            verbose++;
        } else if (!strcmp(argv[1], "-text")) {
            json = 0;
        } else if (!strcmp(argv[1], "-json")) {
            json = 1;
        } else if (!strncmp(argv[1], "-limit=", 7)) {
            limit = 32 * atoi(argv[1] + 7);
        } else if (!strcmp(argv[1], "-msgpipe-io")) {
            with_msgpipe_io = 1;
        } else if (!strcmp(argv[1], "-kthreads")) {
            with_kthreads = 1;
        } else if (!strcmp(argv[1], "-wait-io")) {
            with_waiting = 1;
        } else if (!strcmp(argv[1], "-syscalls")) {
            with_syscalls = 1;
        } else if (!strcmp(argv[1], "-all")) {
            with_msgpipe_io = 1;
            with_kthreads = 1;
            with_waiting = 1;
            with_syscalls = 1;
        } else if (!strcmp(argv[1], "-stats")) {
            show_stats = 1;
        } else if (!strncmp(argv[1], "-onlypid=", 9)) {
            char* next;
            for (char* x = argv[1] + 9; x != NULL; x = next) {
                next = strchr(x, ',');
                if (next) {
                    *next++ = 0;
                }
                visible_pids[visible_count++] = strtoul(x, NULL, 0);
                if (visible_count == MAX_VIS_PIDS) {
                    break;
                }
            }
        } else if (argv[1][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n\n", argv[0]);
            return usage();
        } else {
            break;
        }
        argc--;
        argv++;
    }

    if (argc != 2) {
        return usage();
    }

    int fd;
    if ((fd = open(argv[1], O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[0]);
        return -1;
    }

    if (json) {
#if USE_NS
        printf("{\"displayTimeUnit\":\"ns\",\n"
               "\"metadata\":{\"highres-ticks\":true},\n"
               "\"traceEvents\":[\n");
#else
        printf("[\n");
#endif
    }

    if (with_kthreads) {
        evt_process_name(0, "Magenta Kernel", 0);
    }

    evt_info_t ei;
    while (read(fd, rec.raw, sizeof(ktrace_header_t)) == sizeof(ktrace_header_t)) {
        uint32_t tag = rec.hdr.tag;
        uint32_t len = KTRACE_LEN(tag);
        if (len < sizeof(ktrace_header_t)) {
            fprintf(stderr, "eof: short record\n");
        }
        len -= sizeof(ktrace_header_t);
        if (read(fd, rec.raw + sizeof(ktrace_header_t), len) != len) {
            fprintf(stderr, "eof: short payload\n");
        }
        offset += (sizeof(ktrace_header_t) + len);
        if (tag == 0) {
            fprintf(stderr, "eof: zero tag at offset %08x\n", offset);
            break;
        }
        if (offset > limit) {
            break;
        }
        ei.pid = thread_to_process(rec.hdr.tid);
        ei.tid = rec.hdr.tid;
        ei.ts = ticks_to_ts(rec.hdr.ts);
        if (s.ts_first == 0) {
            s.ts_first = ei.ts;
        }
        s.events++;
        trace_hdr(&ei, tag);
        switch (KTRACE_EVENT(tag)) {
        case EVT_VERSION:
            trace("VERSION      n=%08x\n", rec.x4.a);
            break;
        case EVT_TICKS_PER_MS:
            ticks_per_ms = ((uint64_t)rec.x4.a) | (((uint64_t)rec.x4.b) << 32);
            trace("TICKS_PER_MS n=%lu\n", ticks_per_ms);
            break;
        case EVT_CONTEXT_SWITCH:
            s.context_switch++;
            trace("CTXT_SWITCH to=%08x st=%d cpu=%d old=%08x new=%08x\n",
                  rec.x4.a, rec.x4.b >> 16, rec.x4.b & 0xFFFF, rec.x4.c, rec.x4.d);
            evt_context_switch(&ei, thread_to_process(rec.x4.a), rec.x4.a,
                               rec.x4.b >> 16, rec.x4.b & 0xFFFF, rec.x4.c, rec.x4.d);
            break;
        case EVT_OBJECT_DELETE:
            if ((oi = find_object(rec.x4.a, 0)) == 0) {
                trace("OBJT_DELETE id=%08x\n", rec.x4.a);
            } else {
                trace("%s_DELETE id=%08x\n", kind_string(oi->kind), rec.x4.a);
                switch (oi->kind) {
                case KPIPE:
                    s.msgpipe_del++;
                    evt_msgpipe_delete(&ei, rec.x4.a);
                    break;
                case KTHREAD:
                    s.thread_del++;
                    evt_thread_delete(&ei, rec.x4.a);
                    break;
                case KPROC:
                    s.process_del++;
                    evt_process_delete(&ei, rec.x4.a);
                    break;
                case KPORT:
                    evt_port_delete(&ei, rec.x4.a);
                    break;
                }
            }
            break;
        case EVT_SYSCALL_NAME:
            trace("SYSCALL_NAM id=%08x '%s'\n", rec.name.id, recname(&rec.name));
            break;
        case EVT_KTHREAD_NAME:
            trace("KTHRD_NAME  id=%08x '%s'\n", rec.name.id, recname(&rec.name));
            break;
        case EVT_PROC_CREATE:
            s.process_new++;
            trace("PROC_CREATE id=%08x\n", rec.x4.a);
            oi = new_object(rec.x4.a, KPROC, rec.hdr.tid, 0);
            if (visible_count) {
                oi->flags |= F_INVISIBLE;
                for (unsigned n = 0; n < visible_count; n++) {
                    if (oi->id == visible_pids[n]) {
                        oi->flags &= (~F_INVISIBLE);
                        break;
                    }
                }
            }
            evt_process_create(&ei, rec.x4.a);
            break;
        case EVT_PROC_NAME:
            trace("PROC_NAME   id=%08x '%s'\n", rec.name.id, recname(&rec.name));
            evt_process_name(rec.name.id, recname(&rec.name), 10);
            break;
        case EVT_PROC_START:
            trace("PROC_START  id=%08x tid=%08x\n", rec.x4.b, rec.x4.a);
            evt_process_start(&ei, rec.x4.b, rec.x4.a);
            break;
        case EVT_THREAD_CREATE:
            s.thread_new++;
            trace("THRD_CREATE id=%08x pid=%08x\n", rec.x4.a, rec.x4.b);
            oi = new_object(rec.x4.a, KTHREAD, rec.hdr.tid, rec.x4.b);
            oi2 = find_object(rec.x4.b, KPROC);
            if (oi2 && (oi2->flags & F_INVISIBLE)) {
                oi->flags |= F_INVISIBLE;
            }
            evt_thread_create(&ei, rec.x4.a, rec.x4.b);
            break;
        case EVT_THREAD_NAME:
            trace("THRD_NAME   id=%08x '%s'\n", rec.name.id, recname(&rec.name));
            evt_thread_name(ei.pid, rec.name.id, recname(&rec.name));
            break;
        case EVT_THREAD_START:
            trace("THRD_START  id=%08x\n", rec.x4.a);
            evt_thread_start(&ei, rec.x4.a);
            break;
        case EVT_MSGPIPE_CREATE:
            s.msgpipe_new += 2;
            trace("MPIP_CREATE id=%08x other=%08x flags=%x\n", rec.x4.a, rec.x4.b, rec.x4.c);
            new_object(rec.x4.a, KPIPE, rec.hdr.tid, rec.x4.b);
            new_object(rec.x4.b, KPIPE, rec.hdr.tid, rec.x4.a);
            evt_msgpipe_create(&ei, rec.x4.a, rec.x4.b);
            evt_msgpipe_create(&ei, rec.x4.b, rec.x4.a);
            break;
        case EVT_MSGPIPE_WRITE:
            s.msgpipe_write++;
            n = other_pipe(rec.x4.a);
            trace("MPIP_WRITE  id=%08x to=%08x bytes=%d handles=%d\n", rec.x4.a, n, rec.x4.b, rec.x4.c);
            evt_msgpipe_write(&ei, rec.x4.a, n, rec.x4.b, rec.x4.c);
            break;
        case EVT_MSGPIPE_READ:
            s.msgpipe_read++;
            n = other_pipe(rec.x4.a);
            trace("MPIP_READ   id=%08x fr=%08x bytes=%d handles=%d\n", rec.x4.a, n, rec.x4.b, rec.x4.c);
            evt_msgpipe_read(&ei, rec.x4.a, n, rec.x4.b, rec.x4.c);
            break;
        case EVT_PORT_CREATE:
            trace("PORT_CREATE id=%08x\n", rec.x4.a);
            new_object(rec.x4.a, KPORT, 0, 0);
            evt_port_create(&ei, rec.x4.a);
            break;
        case EVT_PORT_QUEUE:
            trace("PORT_QUEUE  id=%08x\n", rec.x4.a);
            break;
        case EVT_PORT_WAIT:
            trace("PORT_WAIT   id=%08x\n", rec.x4.a);
            evt_port_wait(&ei, rec.x4.a);
            break;
        case EVT_PORT_WAIT_DONE:
            trace("PORT_WDONE  id=%08x\n", rec.x4.a);
            evt_port_wait_done(&ei, rec.x4.a);
            break;
        case EVT_WAIT_ONE:
            t = ((uint64_t)rec.x4.c) | (((uint64_t)rec.x4.d) << 32);
            trace("WAIT_ONE    id=%08x signals=%08x timeout=%lu\n", rec.x4.a, rec.x4.b, t);
            evt_wait_one(&ei, rec.x4.a, rec.x4.b, t);
            break;
        case EVT_WAIT_ONE_DONE:
            trace("WAIT_DONE   id=%08x pending=%08x result=%08x\n", rec.x4.a, rec.x4.b, rec.x4.c);
            evt_wait_one_done(&ei, rec.x4.a, rec.x4.b, rec.x4.c);
            break;
        default:
            trace("UNKNOWN_TAG id=%08x tag=%08x\n", rec.hdr.tid, tag);
            break;
        }
    }
    if (s.events) {
        s.ts_last = ei.ts;
        for_each_object(end_of_trace, ei.ts);
    }

    if (json) {
#if USE_NS
        printf("{}\n]\n}\n");
#else
        printf("{}\n]\n");
#endif
    }

    if (show_stats) {
        dump_stats(&s);
    }
    return 0;
}