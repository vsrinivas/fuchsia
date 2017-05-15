// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <mxio/dispatcher.h>
#include <magenta/listnode.h>

// Eventually we want to use the repeating version of mx_object_wait_async,
// but it is not ready for prime time yet.  This feature flag enables testing.
#define USE_WAIT_ONCE 1

#define VERBOSE_DEBUG 0

#if VERBOSE_DEBUG
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(...) do {} while (0)
#endif

typedef struct {
    list_node_t node;
    mx_handle_t h;
    uint32_t flags;
    mxio_dispatcher_cb_t cb;
    void* func;
    void* cookie;
} handler_t;

#if !USE_WAIT_ONCE
#define FLAG_DISCONNECTED 1
#endif

struct mxio_dispatcher {
    mtx_t lock;
    list_node_t list;
    mx_handle_t ioport;
    mxio_dispatcher_cb_t default_cb;
    thrd_t t;
};

static void mxio_dispatcher_destroy(mxio_dispatcher_t* md) {
    mx_handle_close(md->ioport);
    free(md);
}

static void destroy_handler(mxio_dispatcher_t* md, handler_t* handler, bool need_close_cb) {
    if (need_close_cb) {
        handler->cb(0, handler->func, handler->cookie);
    }

    mx_handle_close(handler->h);
    handler->h = MX_HANDLE_INVALID;

    mtx_lock(&md->lock);
    list_delete(&handler->node);
    mtx_unlock(&md->lock);
    free(handler);
}

// synthetic signal bit for synthetic packet
// used during teardown.
#define SIGNAL_NEEDS_CLOSE_CB 1u

static void disconnect_handler(mxio_dispatcher_t* md, handler_t* handler, bool need_close_cb) {
    xprintf("dispatcher: disconnect: %p / %x\n", handler, handler->h);

#if USE_WAIT_ONCE
    destroy_handler(md, handler, need_close_cb);
#else
    // Cancel the async wait operations.
    mx_status_t r = mx_port_cancel(md->ioport, handler->h, (uint64_t)(uintptr_t)handler);
    if (r) {
        printf("dispatcher: CANCEL FAILED %d\n", r);
    }

    // send a synthetic message so we know when it's safe to destroy
    // TODO: once cancel guarantees no packets will arrive after,
    // we can just destroy the object here instead of doing this...
    mx_port_packet_t packet;
    packet.key = (uint64_t)(uintptr_t)handler;
    packet.signal.observed = need_close_cb ? SIGNAL_NEEDS_CLOSE_CB : 0;
    r = mx_port_queue(md->ioport, &packet, 0);
    if (r) {
        printf("dispatcher: PORT QUEUE FAILED %d\n", r);
    }

    // flag so we know to ignore further events
    handler->flags |= FLAG_DISCONNECTED;
#endif
}

static int mxio_dispatcher_thread(void* _md) {
    mxio_dispatcher_t* md = _md;
    mx_status_t r;
    xprintf("dispatcher: start %p\n", md);

    for (;;) {
        mx_port_packet_t packet;
        if ((r = mx_port_wait(md->ioport, MX_TIME_INFINITE, &packet, 0)) < 0) {
            printf("dispatcher: ioport wait failed %d\n", r);
            break;
        }
        handler_t* handler = (void*)(uintptr_t)packet.key;
#if !USE_WAIT_ONCE
        if (handler->flags & FLAG_DISCONNECTED) {
            // handler is awaiting gc
            // ignore events for it until we get the synthetic "destroy" event
            if (packet.type == MX_PKT_TYPE_USER) {
                destroy_handler(md, handler, packet.signal.observed & SIGNAL_NEEDS_CLOSE_CB);
                printf("dispatcher: destroy %p\n", handler);
            } else {
                printf("dispatcher: spurious packet for %p\n", handler);
            }
            continue;
        }
#endif
        if (packet.signal.observed & MX_CHANNEL_READABLE) {
            if ((r = handler->cb(handler->h, handler->func, handler->cookie)) != 0) {
                if (r == ERR_DISPATCHER_NO_WORK) {
                    printf("mxio: dispatcher found no work to do!\n");
                } else {
                    disconnect_handler(md, handler, r != ERR_DISPATCHER_DONE);
                    continue;
                }
            }
#if USE_WAIT_ONCE
            if ((r = mx_object_wait_async(handler->h, md->ioport, (uint64_t)(uintptr_t)handler,
                                          MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                          MX_WAIT_ASYNC_ONCE)) < 0) {
                printf("dispatcher: could not re-arm: %p\n", handler);
            }
#endif
            continue;
        }
        if (packet.signal.observed & MX_CHANNEL_PEER_CLOSED) {
            // synthesize a close
            disconnect_handler(md, handler, true);
        }
    }

    xprintf("dispatcher: FATAL ERROR, EXITING\n");
    mxio_dispatcher_destroy(md);
    return NO_ERROR;
}

mx_status_t mxio_dispatcher_create(mxio_dispatcher_t** out, mxio_dispatcher_cb_t cb) {
    mxio_dispatcher_t* md;
    if ((md = calloc(1, sizeof(*md))) == NULL) {
        return ERR_NO_MEMORY;
    }
    xprintf("mxio_dispatcher_create: %p\n", md);
    list_initialize(&md->list);
    mtx_init(&md->lock, mtx_plain);
    mx_status_t status;
    if ((status = mx_port_create(MX_PORT_OPT_V2, &md->ioport)) < 0) {
        free(md);
        return status;
    }
    md->default_cb = cb;
    *out = md;
    return NO_ERROR;
}

mx_status_t mxio_dispatcher_start(mxio_dispatcher_t* md, const char* name) {
    mx_status_t r;
    mtx_lock(&md->lock);
    if (md->t == NULL) {
        if (thrd_create_with_name(&md->t, mxio_dispatcher_thread, md, name) != thrd_success) {
            mxio_dispatcher_destroy(md);
            r = ERR_NO_RESOURCES;
        } else {
            thrd_detach(md->t);
            r = NO_ERROR;
        }
    } else {
        r = ERR_BAD_STATE;
    }
    mtx_unlock(&md->lock);
    return r;
}

void mxio_dispatcher_run(mxio_dispatcher_t* md) {
    mxio_dispatcher_thread(md);
}

mx_status_t mxio_dispatcher_add(mxio_dispatcher_t* md, mx_handle_t h, void* func, void* cookie) {
    return mxio_dispatcher_add_etc(md, h, md->default_cb, func, cookie);
}

mx_status_t mxio_dispatcher_add_etc(mxio_dispatcher_t* md, mx_handle_t h,
                                    mxio_dispatcher_cb_t cb,
                                    void* func, void* cookie) {
    handler_t* handler;
    mx_status_t r;

    if ((handler = malloc(sizeof(handler_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    handler->h = h;
    handler->flags = 0;
    handler->cb = cb;
    handler->func = func;
    handler->cookie = cookie;

    mtx_lock(&md->lock);
    list_add_tail(&md->list, &handler->node);
#if USE_WAIT_ONCE
    if ((r = mx_object_wait_async(h, md->ioport, (uint64_t)(uintptr_t)handler,
                                  MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                  MX_WAIT_ASYNC_ONCE)) < 0) {
        list_delete(&handler->node);
    }
#else
    if ((r = mx_object_wait_async(h, md->ioport, (uint64_t)(uintptr_t)handler,
                                  MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                  MX_WAIT_ASYNC_REPEATING)) < 0) {
        list_delete(&handler->node);
    }
#endif
    mtx_unlock(&md->lock);

    if (r < 0) {
        printf("dispatcher: failed to bind: %d\n", r);
        free(handler);
    } else {
        xprintf("dispatcher: added %p / %x\n", handler, h);
    }
    return r;
}
