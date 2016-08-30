// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <magenta/listnode.h>
#include <threads.h>

#define MXDEBUG 0

typedef struct {
    list_node_t node;
    mx_handle_t h;
    void* cb;
    void* cookie;
} handler_t;

#define MAX_HANDLERS 128

struct mxio_dispatcher {
    list_node_t list;
    mx_handle_t tx;
    mx_handle_t rx;
    thrd_t t;
    mxio_dispatcher_cb_t cb;

    mx_handle_t handles[MAX_HANDLERS + 1];
    mx_signals_t wsigs[MAX_HANDLERS + 1];
    mx_signals_states_t states[MAX_HANDLERS + 1];
};

static void mxio_dispatcher_destroy(mxio_dispatcher_t* md) {
    mx_handle_close(md->tx);
    mx_handle_close(md->rx);
    free(md);
}

static void remove_handler(mxio_dispatcher_t* md, handler_t* handler, mx_status_t r) {
    list_delete(&handler->node);
    if (r < 0) {
        md->cb(0, handler->cb, handler->cookie);
    }
    xprintf("handler(%x) done, status=%d\n", handler->h, r);
    mx_handle_close(handler->h);
    free(handler);
}

static int mxio_dispatcher_thread(void* _md) {
    mxio_dispatcher_t* md = _md;
    handler_t* handler;
    mx_status_t r;
    int i, count;

setup:
    count = 0;
    list_for_every_entry (&md->list, handler, handler_t, node) {
        md->handles[count] = handler->h;
        md->wsigs[count] = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
        count++;
    }
    md->handles[count] = md->rx;
    md->wsigs[count] = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;

    xprintf("dispatcher: listening to %d pipe%s\n", count, (count == 1) ? "" : "s");
    for (;;) {
        r = mx_handle_wait_many(count + 1, md->handles, md->wsigs, MX_TIME_INFINITE,
                                      NULL, md->states);
        if (r < 0) {
            xprintf("dispatcher: wait many failed %d\n", r);
            break;
        }
        i = 0;
        list_for_every_entry (&md->list, handler, handler_t, node) {
            if (md->states[i].satisfied & MX_SIGNAL_READABLE) {
                if ((r = md->cb(handler->h, handler->cb, handler->cookie)) != 0) {
                    remove_handler(md, handler, r);
                    goto setup;
                }
            }
            if (md->states[i].satisfied & MX_SIGNAL_PEER_CLOSED) {
                remove_handler(md, handler, ERR_REMOTE_CLOSED);
                goto setup;
            }
            i++;
        }
        if (md->states[count].satisfied & MX_SIGNAL_READABLE) {
            uint32_t sz = sizeof(handler_t);
            handler_t a;
            if ((r = mx_msgpipe_read(md->rx, &a, &sz, NULL, NULL, 0)) < 0) {
                xprintf("dispatcher: read failure on new handle pipe %d\n", r);
                break;
            }
            if (count == MAX_HANDLERS) {
                // TODO: support growing the table, or use waitsets
                md->cb(0, a.cb, a.cookie);
                mx_handle_close(a.h);
                xprintf("dispatcher(%x) discarding handler, out of memory\n", a.h);
                break;
            }
            if ((handler = malloc(sizeof(handler_t))) == NULL) {
                md->cb(0, a.cb, a.cookie);
                mx_handle_close(a.h);
                xprintf("dispatcher(%x) discarding handler, out of memory\n", a.h);
                break;
            }
            memcpy(handler, &a, sizeof(handler_t));
            xprintf("dispatcher(%x) added %p\n", handler->h, handler->cb);
            list_add_tail(&md->list, &handler->node);
            goto setup;
        }
    }

    mxio_dispatcher_destroy(md);
    return NO_ERROR;
}

mx_status_t mxio_dispatcher_create(mxio_dispatcher_t** out, mxio_dispatcher_cb_t cb) {
    mxio_dispatcher_t* md;
    if ((md = malloc(sizeof(*md))) == NULL) {
        return ERR_NO_MEMORY;
    }
    xprintf("mxio_dispatcher_create: %p\n", md);
    list_initialize(&md->list);
    mx_handle_t h[2];
    mx_status_t r = mx_msgpipe_create(h, 0);
    if (r < 0) {
        free(md);
        return r;
    }
    md->tx = h[0];
    md->rx = h[1];
    md->cb = cb;
    *out = md;
    return NO_ERROR;
}

// TODO: protect against double-start
mx_status_t mxio_dispatcher_start(mxio_dispatcher_t* md) {
    if (thrd_create_with_name(&md->t, mxio_dispatcher_thread, md, "mxio-dispatcher") != thrd_success) {
        mxio_dispatcher_destroy(md);
        return ERR_NO_RESOURCES;
    }
    thrd_detach(md->t);
    return NO_ERROR;
}

// TODO: protect against double-start
void mxio_dispatcher_run(mxio_dispatcher_t* md) {
    mxio_dispatcher_thread(md);
}

// TODO: error in the event of dispatcher out of resources?
mx_status_t mxio_dispatcher_add(mxio_dispatcher_t* md, mx_handle_t h, void* cb, void* cookie) {
    handler_t handler;
    mx_status_t r;

    handler.h = h;
    handler.cb = cb;
    handler.cookie = cookie;
    if ((r = mx_msgpipe_write(md->tx, &handler, sizeof(handler), NULL, 0, 0)) < 0) {
        return r;
    }
    return 0;
}
