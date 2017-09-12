// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <fdio/dispatcher.h>
#include <zircon/listnode.h>

// Eventually we want to use the repeating version of zx_object_wait_async,
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
    zx_handle_t h;
    uint32_t flags;
    fdio_dispatcher_cb_t cb;
    void* func;
    void* cookie;
} handler_t;

#if !USE_WAIT_ONCE
#define FLAG_DISCONNECTED 1
#endif

struct fdio_dispatcher {
    mtx_t lock;
    list_node_t list;
    zx_handle_t port;
    fdio_dispatcher_cb_t default_cb;
    thrd_t t;
};

static void fdio_dispatcher_destroy(fdio_dispatcher_t* md) {
    zx_handle_close(md->port);
    free(md);
}

static void destroy_handler(fdio_dispatcher_t* md, handler_t* handler, bool need_close_cb) {
    if (need_close_cb) {
        handler->cb(0, handler->func, handler->cookie);
    }

    zx_handle_close(handler->h);
    handler->h = ZX_HANDLE_INVALID;

    mtx_lock(&md->lock);
    list_delete(&handler->node);
    mtx_unlock(&md->lock);
    free(handler);
}

// synthetic signal bit for synthetic packet
// used during teardown.
#define SIGNAL_NEEDS_CLOSE_CB 1u

static void disconnect_handler(fdio_dispatcher_t* md, handler_t* handler, bool need_close_cb) {
    xprintf("dispatcher: disconnect: %p / %x\n", handler, handler->h);

#if USE_WAIT_ONCE
    destroy_handler(md, handler, need_close_cb);
#else
    // Cancel the async wait operations.
    zx_status_t r = zx_port_cancel(md->port, handler->h, (uint64_t)(uintptr_t)handler);
    if (r) {
        printf("dispatcher: CANCEL FAILED %d\n", r);
    }

    // send a synthetic message so we know when it's safe to destroy
    // TODO: once cancel guarantees no packets will arrive after,
    // we can just destroy the object here instead of doing this...
    zx_port_packet_t packet;
    packet.key = (uint64_t)(uintptr_t)handler;
    packet.signal.observed = need_close_cb ? SIGNAL_NEEDS_CLOSE_CB : 0;
    r = zx_port_queue(md->port, &packet, 0);
    if (r) {
        printf("dispatcher: PORT QUEUE FAILED %d\n", r);
    }

    // flag so we know to ignore further events
    handler->flags |= FLAG_DISCONNECTED;
#endif
}

static int fdio_dispatcher_thread(void* _md) {
    fdio_dispatcher_t* md = _md;
    zx_status_t r;
    xprintf("dispatcher: start %p\n", md);

    for (;;) {
        zx_port_packet_t packet;
        if ((r = zx_port_wait(md->port, ZX_TIME_INFINITE, &packet, 0)) < 0) {
            printf("dispatcher: port wait failed %d\n", r);
            break;
        }
        handler_t* handler = (void*)(uintptr_t)packet.key;
#if !USE_WAIT_ONCE
        if (handler->flags & FLAG_DISCONNECTED) {
            // handler is awaiting gc
            // ignore events for it until we get the synthetic "destroy" event
            if (packet.type == ZX_PKT_TYPE_USER) {
                destroy_handler(md, handler, packet.signal.observed & SIGNAL_NEEDS_CLOSE_CB);
                printf("dispatcher: destroy %p\n", handler);
            } else {
                printf("dispatcher: spurious packet for %p\n", handler);
            }
            continue;
        }
#endif
        if (packet.signal.observed & ZX_CHANNEL_READABLE) {
            if ((r = handler->cb(handler->h, handler->func, handler->cookie)) != 0) {
                if (r == ERR_DISPATCHER_NO_WORK) {
                    printf("fdio: dispatcher found no work to do!\n");
                } else {
                    disconnect_handler(md, handler, r != ERR_DISPATCHER_DONE);
                    continue;
                }
            }
#if USE_WAIT_ONCE
            if ((r = zx_object_wait_async(handler->h, md->port, (uint64_t)(uintptr_t)handler,
                                          ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                          ZX_WAIT_ASYNC_ONCE)) < 0) {
                printf("dispatcher: could not re-arm: %p\n", handler);
            }
#endif
            continue;
        }
        if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
            // synthesize a close
            disconnect_handler(md, handler, true);
        }
    }

    xprintf("dispatcher: FATAL ERROR, EXITING\n");
    fdio_dispatcher_destroy(md);
    return ZX_OK;
}

zx_status_t fdio_dispatcher_create(fdio_dispatcher_t** out, fdio_dispatcher_cb_t cb) {
    fdio_dispatcher_t* md;
    if ((md = calloc(1, sizeof(*md))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    xprintf("fdio_dispatcher_create: %p\n", md);
    list_initialize(&md->list);
    mtx_init(&md->lock, mtx_plain);
    zx_status_t status;
    if ((status = zx_port_create(0, &md->port)) < 0) {
        free(md);
        return status;
    }
    md->default_cb = cb;
    *out = md;
    return ZX_OK;
}

zx_status_t fdio_dispatcher_start(fdio_dispatcher_t* md, const char* name) {
    zx_status_t r;
    mtx_lock(&md->lock);
    if (md->t == NULL) {
        if (thrd_create_with_name(&md->t, fdio_dispatcher_thread, md, name) != thrd_success) {
            fdio_dispatcher_destroy(md);
            r = ZX_ERR_NO_RESOURCES;
        } else {
            thrd_detach(md->t);
            r = ZX_OK;
        }
    } else {
        r = ZX_ERR_BAD_STATE;
    }
    mtx_unlock(&md->lock);
    return r;
}

void fdio_dispatcher_run(fdio_dispatcher_t* md) {
    fdio_dispatcher_thread(md);
}

zx_status_t fdio_dispatcher_add(fdio_dispatcher_t* md, zx_handle_t h, void* func, void* cookie) {
    return fdio_dispatcher_add_etc(md, h, md->default_cb, func, cookie);
}

zx_status_t fdio_dispatcher_add_etc(fdio_dispatcher_t* md, zx_handle_t h,
                                    fdio_dispatcher_cb_t cb,
                                    void* func, void* cookie) {
    handler_t* handler;
    zx_status_t r;

    if ((handler = malloc(sizeof(handler_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    handler->h = h;
    handler->flags = 0;
    handler->cb = cb;
    handler->func = func;
    handler->cookie = cookie;

    mtx_lock(&md->lock);
    list_add_tail(&md->list, &handler->node);
#if USE_WAIT_ONCE
    if ((r = zx_object_wait_async(h, md->port, (uint64_t)(uintptr_t)handler,
                                  ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                  ZX_WAIT_ASYNC_ONCE)) < 0) {
        list_delete(&handler->node);
    }
#else
    if ((r = zx_object_wait_async(h, md->port, (uint64_t)(uintptr_t)handler,
                                  ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                  ZX_WAIT_ASYNC_REPEATING)) < 0) {
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
