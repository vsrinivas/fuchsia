/*
 * Copyright (c) 2018 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "workqueue.h"

#include <pthread.h>
#include <string.h>
#define _ALL_SOURCE // to get MTX_INIT from threads.h
#include <threads.h>

#include <lib/sync/completion.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "debug.h"

typedef void (*work_handler_t)(struct work_struct* work);

#define WORKQUEUE_SIGNAL ZX_USER_SIGNAL_0

// lock: Held when accessing list, current, or contents of current.
// list: Pending work (not including current work).
// current: Currently executing work, or NULL.
// work_ready: Signaled to tell worker to start.
// name: May be used for debugging.
// thread: The worker thread.
struct workqueue_struct {
    // TODO(NET-780): Convert to C++ and add locking annotations.
    mtx_t lock;
    list_node_t list;
    struct work_struct* current;
    sync_completion_t work_ready;
    char name[WORKQUEUE_NAME_MAXLEN];
    pthread_t thread;
};

static struct workqueue_struct default_workqueue = {.lock = MTX_INIT};

void workqueue_init_work(struct work_struct* work, work_handler_t handler) {
    if (work == NULL) {
        return;
    }
    work->handler = handler;
    work->signaler = ZX_HANDLE_INVALID;
    list_initialize(&work->item);
    work->workqueue = NULL;
}

static void kill_this_workqueue(struct work_struct* work) {
    pthread_exit(NULL);
}

void workqueue_destroy(struct workqueue_struct* workqueue) {
    if (workqueue == NULL) {
        return;
    }

    struct work_struct work;
    workqueue_init_work(&work, kill_this_workqueue);
    workqueue_schedule(workqueue, &work);
    pthread_join(workqueue->thread, NULL);
    mtx_destroy(&workqueue->lock);

    free(workqueue);
}

static void workqueue_nop(struct work_struct* work) {
}

void workqueue_flush(struct workqueue_struct* workqueue) {
    if (workqueue == NULL) {
        return;
    }

    struct work_struct work;
    workqueue_init_work(&work, workqueue_nop);
    zx_status_t result;
    result = zx_event_create(0, &work.signaler);
    if (result != ZX_OK) {
        brcmf_err("Failed to create signal (work not canceled)");
        return;
    }
    workqueue_schedule(workqueue, &work);
    zx_signals_t observed;
    result = zx_object_wait_one(work.signaler, WORKQUEUE_SIGNAL, ZX_TIME_INFINITE, &observed);
    if (result != ZX_OK || (observed & WORKQUEUE_SIGNAL) == 0) {
        brcmf_err("Bad return from wait (work likely not flushed): result %d, observed %x",
                    result, observed);
    }
    zx_handle_close(work.signaler);
}

void workqueue_flush_default(void) {
    workqueue_flush(&default_workqueue);
}

void workqueue_cancel_work(struct work_struct* work) {
    if (work == NULL) {
        return;
    }

    struct workqueue_struct* workqueue = work->workqueue;
    if (workqueue == NULL) {
        return;
    }
    zx_status_t result;
    mtx_lock(&workqueue->lock);
    if (workqueue->current == work) {
        result = zx_event_create(0, &work->signaler);
        mtx_unlock(&workqueue->lock);
        if (result != ZX_OK) {
            brcmf_err("Failed to create signal (work not canceled)");
            return;
        }
        zx_signals_t observed;
        result = zx_object_wait_one(work->signaler, WORKQUEUE_SIGNAL, ZX_TIME_INFINITE, &observed);
        if (result != ZX_OK || (observed & WORKQUEUE_SIGNAL) == 0) {
            brcmf_err("Bad return from wait (work likely not canceled): result %d, observed %x",
                      result, observed);
        }
        mtx_lock(&workqueue->lock);
        zx_handle_close(work->signaler);
        work->signaler = ZX_HANDLE_INVALID;
        mtx_unlock(&workqueue->lock);
        return;
    } else {
        list_node_t* node;
        list_node_t* temp_node;
        list_for_every_safe(&(workqueue->list), node, temp_node) {
            if (node == &work->item) {
                list_delete(node);
                mtx_unlock(&workqueue->lock);
                return;
            }
        }
        mtx_unlock(&workqueue->lock);
        brcmf_dbg(TEMP, "Work to be canceled not found");
    }
}

static void* workqueue_runner(void* arg) {
    struct workqueue_struct* workqueue = (struct workqueue_struct*) arg;

    while(1) {
        sync_completion_wait(&workqueue->work_ready, ZX_TIME_INFINITE);
        sync_completion_reset(&workqueue->work_ready);
        struct work_struct* work;
        list_node_t* item;
        mtx_lock(&workqueue->lock);
        item = list_remove_head(&workqueue->list);
        work = (item == NULL) ? NULL : containerof(item, struct work_struct, item);
        workqueue->current = work;
        mtx_unlock(&workqueue->lock);
        while (work != NULL) {
            work->handler(workqueue->current);
            mtx_lock(&workqueue->lock);
            work->workqueue = NULL;
            if (work->signaler != ZX_HANDLE_INVALID) {
                zx_object_signal(work->signaler, 0, WORKQUEUE_SIGNAL);
            }
            item = list_remove_head(&workqueue->list);
            work = (item == NULL) ? NULL : containerof(item, struct work_struct, item);
            workqueue->current = work;
            mtx_unlock(&workqueue->lock);
        }
    }
    return NULL;
}

void workqueue_schedule(struct workqueue_struct* workqueue, struct work_struct* work) {
    if (workqueue == NULL) {
        return;
    }
    if (work == NULL) {
        return;
    }

    list_node_t* node;
    mtx_lock(&workqueue->lock);
    work->workqueue = workqueue;
    if (workqueue->current == work) {
        mtx_unlock(&workqueue->lock);
        return;
    }
    list_for_every(&workqueue->list, node) {
        if (node == &work->item) {
            mtx_unlock(&workqueue->lock);
            return;
        }
    }
    list_add_tail(&workqueue->list, &work->item);
    sync_completion_signal(&workqueue->work_ready);
    mtx_unlock(&workqueue->lock);
}

static void start_workqueue(struct workqueue_struct* workqueue, const char* name) {
    strlcpy(workqueue->name, name, WORKQUEUE_NAME_MAXLEN);
    workqueue->work_ready = SYNC_COMPLETION_INIT;
    list_initialize(&workqueue->list);
    workqueue->current = NULL;
    pthread_create(&workqueue->thread, NULL, workqueue_runner, workqueue);
}

void workqueue_schedule_default(struct work_struct* work) {
    if (work == NULL) {
        return;
    }
    mtx_lock(&default_workqueue.lock);
    if (!default_workqueue.thread) {
        start_workqueue(&default_workqueue, "default_workqueue");
    }
    mtx_unlock(&default_workqueue.lock);
    workqueue_schedule(&default_workqueue, work);
}

struct workqueue_struct* workqueue_create(const char* name) {
    struct workqueue_struct* workqueue;

    workqueue = calloc(1, sizeof(*workqueue));
    if (workqueue == NULL) {
        return NULL;
    }
    if (name == NULL) {
        start_workqueue(workqueue, "nameless");
    } else {
        start_workqueue(workqueue, name);
    }
    return workqueue;
}
