// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <magenta/compiler.h>
#include <magenta/listnode.h>
#include <magenta/types.h>

#include "filesystems.h"

#define FAIL -1
#define DONE 0

#define BLKSIZE 8192
#define FBUFSIZE 65536

typedef struct worker worker_t;
typedef struct random_op random_op_t;

typedef struct env {
    worker_t* all_workers;

    mtx_t log_timer_lock;
    cnd_t log_timer_cnd;

    random_op_t* ops;
    unsigned n_ops;

    bool tests_finished;

    list_node_t threads;

    bool debug;
} env_t;

typedef struct worker {
    worker_t* next;

    env_t* env;

    int fd;
    ssize_t size;

    char name[PATH_MAX];
    unsigned seed;

    unsigned opcnt;
} worker_t;

static bool init_environment(env_t*);
static void add_random_ops(env_t* env);

typedef struct thread_list {
    list_node_t node;
    thrd_t t;
} thread_list_t;

static bool worker_new(env_t* env, const char* fn, uint32_t size) {
    worker_t* w = calloc(1, sizeof(worker_t));
    ASSERT_NEQ(w, NULL, "");

    // per-thread random seed
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    w->seed = (int)ts.tv_nsec;

    w->env = env;

    snprintf(w->name, sizeof(w->name), "%s", fn);
    w->size = size;
    w->fd = -1;

    w->opcnt = 0;

    w->next = env->all_workers;
    env->all_workers = w;

    return true;
}

static void free_workers(env_t* env) {
    worker_t* all_workers = env->all_workers;
    for (worker_t* w = all_workers; w != NULL;) {
        worker_t* next = w->next;
        free(w);
        w = next;
    }
}

#define KB(n) ((n) * 1024)
#define MB(n) ((n) * 1024 * 1024)

static struct {
    const char* name;
    uint32_t size;
} WORK[] = {
    // one thread per work entry
    { "thd0000", KB(5)},
    { "thd0001", MB(10)},
    { "thd0002", KB(512)},
    { "thd0003", KB(512)},
    { "thd0004", KB(512)},
    { "thd0005", MB(20)},
    { "thd0006", KB(512)},
    { "thd0007", KB(512)},
};

static bool init_environment(env_t* env) {
    // tests are run repeatedly, so reinitialize each time
    env->all_workers = NULL;

    list_initialize(&env->threads);

    mtx_init(&env->log_timer_lock, mtx_plain);
    cnd_init(&env->log_timer_cnd);

    env->tests_finished = false;
    env->debug = false;

    add_random_ops(env);

    // assemble the work
    for (unsigned n = 0; n < countof(WORK); n++) {
        ASSERT_TRUE(worker_new(env, WORK[n].name, WORK[n].size), "");
    }

    return true;
}

// wait until test finishes or timer suggests we may be hung
static const unsigned TEST_MAX_RUNTIME = 120; // 120 sec max
static int log_timer(void* arg) {
    env_t* env = arg;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += TEST_MAX_RUNTIME;

    mtx_lock(&env->log_timer_lock);
    cnd_timedwait(&env->log_timer_cnd, &env->log_timer_lock, &ts);
    if (!env->tests_finished) {
        exit(1); // causes remaining threads to abort
    }
    mtx_unlock(&env->log_timer_lock);
    return 0;
}

static void task_debug_op(worker_t* w, const char* fn) {
    env_t* env = w->env;

    w->opcnt++;
    if (env->debug) {
        fprintf(stderr, "%s[%d] %s\n", w->name, w->opcnt, fn);
    }
}

static void task_error(worker_t* w, const char* fn, const char* msg) {
    int errnum = errno;
    char buf[128];
    strerror_r(errnum, buf, sizeof(buf));
    fprintf(stderr, "%s ERROR %s(%s): %s(%d)\n", w->name, fn,
            msg, buf, errnum);
}

static int task_create_a(worker_t* w) {
    // put a page of data into ::/a
    task_debug_op(w, "t: create_a");
    int fd = open("::/a", O_RDWR+O_CREAT, 0666);
    if (fd < 0) {
        // errno may be one of EEXIST
        if (errno != EEXIST) {
            task_error(w, "t: create_a", "open");
            return FAIL;
        }
    } else {
        char buf[BLKSIZE];
        memset(buf, 0xab, sizeof(buf));
        ssize_t len = write(fd, buf, sizeof(buf));
        if (len < 0) {
            task_error(w, "t: create_a", "write");
            return FAIL;
        }
        assert(len == sizeof(buf));
        EXPECT_EQ(close(fd), 0, "");
    }
    return DONE;
}

static int task_create_b(worker_t* w) {
    // put a page of data into ::/b
    task_debug_op(w, "t: create_b");
    int fd = open("::/b", O_RDWR+O_CREAT, 0666);
    if (fd < 0) {
        // errno may be one of EEXIST
        if (errno != EEXIST) {
            task_error(w, "t: create_b", "open");
            return FAIL;
        }
    } else {
        char buf[BLKSIZE];
        memset(buf, 0xba, sizeof(buf));
        ssize_t len = write(fd, buf, sizeof(buf));
        if (len < 0) {
            task_error(w, "t: create_a", "write");
            return FAIL;
        }
        assert(len == sizeof(buf));
        EXPECT_EQ(close(fd), 0, "");
    }
    return DONE;
}

static int task_rename_ab(worker_t* w) {
    // rename ::/a -> ::/b
    task_debug_op(w, "t: rename_ab");
    int rc = rename("::/a", "::/b");
    if (rc < 0) {
        // errno may be one of ENOENT
        if (errno != ENOENT) {
            task_error(w, "t: rename_ab", "rename");
            return FAIL;
        }
    }
    return DONE;
}

static int task_rename_ba(worker_t* w) {
    // rename ::/b -> ::/a
    task_debug_op(w, "t: rename_ba");
    int rc = rename("::/b", "::/a");
    if (rc < 0) {
        // errno may be one of ENOENT
        if (errno != ENOENT) {
            task_error(w, "t: rename_ba", "rename");
            return FAIL;
        }
    }
    return DONE;
}

static int task_make_private_dir(worker_t* w) {
    // mkdir ::/threadname
    task_debug_op(w, "t: make_private_dir");
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s", w->name);
    int rc = mkdir(fname, 0755);
    if (rc < 0) {
        // errno may be one of EEXIST, ENOENT
        if (errno != ENOENT && errno != EEXIST) {
            task_error(w, "t: make_private_dir", "mkdir");
            return FAIL;
        }
    }
    return DONE;
}

static int task_rmdir_private_dir(worker_t* w) {
    // unlink ::/threadname
    task_debug_op(w, "t: remove_private_dir");
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s", w->name);
    int rc = rmdir(fname);
    if (rc < 0) {
        // errno may be one of ENOENT, ENOTEMPTY,
        if (errno != ENOENT && errno != ENOTEMPTY) {
            task_error(w, "t: remove_private_dir", "rmdir");
            return FAIL;
        }
    }
    return DONE;
}

static int task_unlink_a(worker_t* w) {
    // unlink ::/a
    task_debug_op(w, "t: unlink_a");
    int rc = unlink("::/a");
    if (rc < 0) {
        // errno may be one of ENOENT
        if (errno != ENOENT) {
            task_error(w, "t: unlink_a", "unlink");
            return FAIL;
        }
    }
    return DONE;
}

static int task_unlink_b(worker_t* w) {
    // unlink ::/b
    task_debug_op(w, "t: unlink_b");
    int rc = unlink("::/b");
    if (rc < 0) {
        // errno may be one of ENOENT
        if (errno != ENOENT) {
            task_error(w, "t: unlink_b", "unlink");
            return FAIL;
        }
    }
    return DONE;
}

static int task_mkdir_private_b(worker_t* w) {
    // mkdir ::/threadname/b
    task_debug_op(w, "t: mkdir_private_b");
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s/b", w->name);
    int rc = mkdir(fname, 0755);
    if (rc < 0) {
        // errno may be one of EEXIST, ENOENT, ENOTDIR
        if (errno != ENOENT && errno != ENOENT && errno != ENOTDIR) {
            task_error(w, "t: mkdir_private_b", "mkdir");
            return FAIL;
        }
    }
    return DONE;
}

static int task_rmdir_private_b(worker_t* w) {
    // unlink ::/threadname/b
    task_debug_op(w, "t: rmdir_private_b");
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s/b", w->name);
    int rc = rmdir(fname);
    if (rc < 0) {
        // errno may be one of ENOENT, ENOTDIR, ENOTEMPTY
        if (errno != ENOENT && errno != ENOTDIR && errno != ENOTDIR) {
            task_error(w, "t: rmdir_private_b", "rmdir");
            return FAIL;
        }
    }
    return DONE;
}

static int task_move_a_to_private(worker_t* w) {
    // mv ::/a -> ::/threadname/a
    task_debug_op(w, "t: mv_a_to__private");
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s/a", w->name);
    int rc = rename("::/a", fname);
    if (rc < 0) {
        // errno may be one of EEXIST, ENOENT, ENOTDIR
        if (errno != EEXIST && errno != ENOENT && errno != ENOTDIR) {
            task_error(w, "t: mv_a_to__private", "rename");
            return FAIL;
        }
    }
    return DONE;
}

static int task_write_private_b(worker_t* w) {
    // put a page of data into ::/threadname/b
    task_debug_op(w, "t: write_private_b");
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s/b", w->name);
    int fd = open(fname, O_RDWR+O_EXCL+O_CREAT, 0666);
    if (fd < 0) {
        // errno may be one of ENOENT, EISDIR, ENOTDIR, EEXIST
        if (errno != ENOENT && errno != EISDIR &&
            errno != ENOTDIR && errno != EEXIST) {
            task_error(w, "t: write_private_b", "open");
            return FAIL;
        }
    } else {
        char buf[BLKSIZE];
        memset(buf, 0xba, sizeof(buf));
        ssize_t len = write(fd, buf, sizeof(buf));
        if (len < 0) {
            task_error(w, "t: write_private_b", "write");
            return FAIL;
        }
        assert(len == sizeof(buf));
        EXPECT_EQ(close(fd), 0, "");
    }
    return DONE;
}

static int task_rename_private_ba(worker_t* w) {
    // move ::/threadname/b -> ::/a
    task_debug_op(w, "t: rename_private_ba");
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s/b", w->name);
    int rc = rename(fname, "::/a");
    if (rc < 0) {
        // errno may be one of EEXIST, ENOENT
        if (errno != EEXIST && errno != ENOENT) {
            task_error(w, "t: rename_private_ba", "rename");
            return FAIL;
        }
    }
    return DONE;
}

static int task_rename_private_ab(worker_t* w) {
    // move ::/threadname/a -> ::/b
    task_debug_op(w, "t: rename_private_ab");
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s/a", w->name);
    int rc = rename(fname, "::/b");
    if (rc < 0) {
        // errno may be one of EEXIST, ENOENT
        if (errno != EEXIST && errno != ENOENT) {
            task_error(w, "t: rename_private_ab", "rename");
            return FAIL;
        }
    }
    return DONE;
}

static int task_open_private_a(worker_t* w) {
    // close(fd); fd <- open("::/threadhame/a")
    task_debug_op(w, "t: open_private_a");
    if (w->fd >= 0) {
        EXPECT_EQ(close(w->fd), 0, "");
    }
    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "::/%s/a", w->name);
    w->fd = open(fname, O_RDWR+O_CREAT+O_EXCL, 0666);
    if (w->fd < 0) {
        if (errno == EEXIST) {
            w->fd = open(fname, O_RDWR+O_EXCL);
            if (w->fd < 0) {
                task_error(w, "t: open_private_a", "open-existing");
                return FAIL;
            }
        } else {
            // errno may be one of EEXIST, ENOENT
            if (errno != ENOENT) {
                task_error(w, "t: open_private_a", "open");
                return FAIL;
            }
        }
    }
    return DONE;
}

static int task_close_fd(worker_t* w) {
    // close(fd)
    task_debug_op(w, "t: close_fd");
    if (w->fd >= 0) {
        int rc = close(w->fd);
        if (rc < 0) {
            // errno may be one of ??
            task_error(w, "t: close_fd", "close");
            return FAIL;
        }
        w->fd = -1;
    }
    return DONE;
}

static int task_write_fd_big(worker_t* w) {
    // write(fd, big buffer, ...)
    task_debug_op(w, "t: write_fd_big");
    if (w->fd >= 0) {
        char buf[FBUFSIZE];
        memset(buf, 0xab, sizeof(buf));
        ssize_t len = write(w->fd, buf, sizeof(buf));
        if (len < 0) {
            // errno may be one of ??
            task_error(w, "t: write_fd_small", "write");
            return FAIL;
        } else {
            assert(len == sizeof(buf));
            off_t off = lseek(w->fd, 0, SEEK_CUR);
            assert(off >= 0);
            if (off >= w->size) {
                off = lseek(w->fd, 0, SEEK_SET);
                assert(off == 0);
            }
        }
    }
    return DONE;
}

static int task_write_fd_small(worker_t* w) {
    // write(fd, small buffer, ...)
    task_debug_op(w, "t: write_fd_small");
    if (w->fd >= 0) {
        char buf[BLKSIZE];
        memset(buf, 0xab, sizeof(buf));
        ssize_t len = write(w->fd, buf, sizeof(buf));
        if (len < 0) {
            // errno may be one of ??
            task_error(w, "t: write_fd_small", "write");
            return FAIL;
        } else {
            assert(len == sizeof(buf));
            off_t off = lseek(w->fd, 0, SEEK_CUR);
            assert(off >= 0);
            if (off >= w->size) {
                off = lseek(w->fd, 0, SEEK_SET);
                assert(off == 0);
            }
        }
    }
    return DONE;
}

static int task_truncate_fd(worker_t* w) {
    // ftruncate(fd)
    task_debug_op(w, "t: truncate_fd");
    if (w->fd >= 0) {
        int rc = ftruncate(w->fd, 0);
        if (rc < 0) {
            // errno may be one of ??
            task_error(w, "t: truncate_fd", "truncate");
            return FAIL;
        }
    }
    return DONE;
}

static int task_utime_fd(worker_t* w) {
    // utime(fd)
    task_debug_op(w, "t: utime_fd");
    if (w->fd >= 0) {
        struct timespec ts[2] = {
            { .tv_nsec = UTIME_OMIT }, // no atime
            { .tv_nsec = UTIME_NOW }, // mtime == now
        };
        int rc = futimens(w->fd, ts);
        if (rc < 0) {
            task_error(w, "t: utime_fd", "futimens");
            return FAIL;
        }
    }
    return DONE;
}

static int task_seek_fd_end(worker_t* w) {
    task_debug_op(w, "t: seek_fd_end");
    if (w->fd >= 0) {
        int rc = lseek(w->fd, 0, SEEK_END);
        if (rc < 0) {
            // errno may be one of ??
            task_error(w, "t: seek_fd_end", "lseek");
            return FAIL;
        }
    }
    return DONE;
}

static int task_seek_fd_start(worker_t* w) {
    // fseek(fd, SEEK_SET, 0)
    task_debug_op(w, "t: seek_fd_start");
    if (w->fd >= 0) {
        int rc = lseek(w->fd, 0, SEEK_SET);
        if (rc < 0) {
            // errno may be one of ??
            task_error(w, "t: seek_fd_start", "lseek");
            return FAIL;
        }
    }
    return DONE;
}

static int task_truncate_a(worker_t* w) {
    // truncate("::/a")
    int rc = truncate("::/a", 0);
    if (rc < 0) {
        // errno may be one of ENOENT
        if (errno != ENOENT) {
            task_error(w, "t: truncate_a", "truncate");
            return FAIL;
        }
    }
    return DONE;
}

struct random_op {
    const char* name;
    int (*fn)(worker_t*);
    unsigned weight;
} OPS[] = {
    {"task_create_a", task_create_a, 1},
    {"task_create_b", task_create_b, 1},
    {"task_rename_ab", task_rename_ab, 4},
    {"task_rename_ba", task_rename_ba, 4},
    {"task_make_private_dir", task_make_private_dir, 4},
    {"task_move_a_to_private", task_move_a_to_private, 1},
    {"task_write_private_b", task_write_private_b, 1},
    {"task_rename_private_ba", task_rename_private_ba, 1},
    {"task_rename_private_ab", task_rename_private_ab, 1},
    {"task_open_private_a", task_open_private_a, 5},
    {"task_close_fd", task_close_fd, 2},
    {"task_write_fd_big", task_write_fd_big, 20},
    {"task_write_fd_small", task_write_fd_small, 20},
    {"task_truncate_fd", task_truncate_fd, 2},
    {"task_utime_fd", task_utime_fd, 2},
    {"task_seek_fd", task_seek_fd_start, 2},
    {"task_seek_fd_end", task_seek_fd_end, 2},
    {"task_truncate_a", task_truncate_a, 1},
};

// create a weighted list of operations for each thread
static void add_random_ops(env_t* env) {
    // expand the list of n*countof(OPS) operations weighted appropriately
    int n_ops = 0;
    for (unsigned i=0; i<countof(OPS); i++) {
        n_ops += OPS[i].weight;
    }
    random_op_t* ops_list = malloc(n_ops * sizeof(random_op_t));
    assert(ops_list != NULL);

    unsigned op = 0;
    for (unsigned i=0; i<countof(OPS); i++) {
        unsigned n_op = OPS[i].weight;
        for (unsigned j=0; j<n_op; j++) {
            ops_list[op++] = OPS[i];
        }
    }
    env->ops = ops_list;
    env->n_ops = n_ops;
}

static const unsigned N_SERIAL_OPS = 4; // yield every 1/n ops

const unsigned MAX_OPS = 1000;
static int do_random_ops(void* arg) {
    worker_t* w = arg;
    env_t* env = w->env;

    // for some big number of operations
    // do an operation and yield, repeat
    for (unsigned i=0; i<MAX_OPS; i++) {
        unsigned idx = rand_r(&w->seed) % env->n_ops;
        random_op_t* op = env->ops+idx;

        if (op->fn(w) != DONE) {
            fprintf(stderr, "%s: op %s failed\n", w->name, op->name);
            exit(1);
        }
        if ((idx % N_SERIAL_OPS) != 0)
            thrd_yield();
    }

    // Close the worker's personal fd (if it is open) and
    // unlink the worker directory.
    fprintf(stderr, "work thread(%s) done\n", w->name);
    task_close_fd(w);
    unlink(w->name);

    // currently, threads either return DONE or exit the test
    return DONE;
}

bool test_random_op_multithreaded(void) {
    BEGIN_TEST;

    env_t env;
    ASSERT_TRUE(init_environment(&env), "");

    for (worker_t* w = env.all_workers; w != NULL; w = w->next) {
        // start the workers on separate threads
        thrd_t t;
        ASSERT_EQ(thrd_create(&t, do_random_ops, w), thrd_success, "");

        thread_list_t* thread = malloc(sizeof(thread_list_t));
        ASSERT_NEQ(thread, NULL, "");
        thread->t = t;
        list_add_tail(&env.threads, &thread->node);
    }

    thrd_t timer_thread;
    ASSERT_EQ(thrd_create(&timer_thread, log_timer, &env), thrd_success, "");

    thread_list_t* next;
    thread_list_t* prev = NULL;
    list_for_every_entry(&env.threads, next, thread_list_t, node) {
        int rc;
        ASSERT_EQ(thrd_join(next->t, &rc), thrd_success, "");
        ASSERT_EQ(rc, DONE, "Background thread joined, but failed");
        if (prev) {
            free(prev);
        }
        prev = next;
    }
    if (prev) {
        free(prev);
    }

    // signal to timer that all threads have finished
    mtx_lock(&env.log_timer_lock);
    env.tests_finished = true;
    mtx_unlock(&env.log_timer_lock);

    ASSERT_EQ(cnd_broadcast(&env.log_timer_cnd), thrd_success, "");

    int rc;
    ASSERT_EQ(thrd_join(timer_thread, &rc), thrd_success, "");
    ASSERT_EQ(rc, 0, "Timer thread failed");

    free(env.ops);
    free_workers(&env);

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(random_ops_tests,
    RUN_TEST_LARGE(test_random_op_multithreaded)
)
