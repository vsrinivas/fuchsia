// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <magenta/listnode.h>

#define MXDEBUG 0

typedef struct {
    list_node_t node;
    mx_handle_t h;
    uint32_t flags;
    void* cb;
    void* cookie;
} handler_t;

#define FLAG_DISCONNECTED 1

struct mxio_dispatcher {
    mtx_t lock;
    list_node_t list;
    mx_handle_t ioport;
    mxio_dispatcher_cb_t cb;
    thrd_t t;
};

static void mxio_dispatcher_destroy(mxio_dispatcher_t* md) {
    mx_handle_close(md->ioport);
    free(md);
}

static void destroy_handler(mxio_dispatcher_t* md, handler_t* handler, bool need_close_cb) {
    if (need_close_cb) {
        md->cb(0, handler->cb, handler->cookie);
    }
    mtx_lock(&md->lock);
    list_delete(&handler->node);
    mtx_unlock(&md->lock);
    free(handler);
}

static void disconnect_handler(mxio_dispatcher_t* md, handler_t* handler, bool need_close_cb) {
    // close handle, so we get no further messages
    mx_handle_close(handler->h);

    // send a synthetic message so we know when it's safe to destroy
    mx_io_packet_t packet;
    packet.hdr.key = (uint64_t)(uintptr_t)handler;
    packet.signals = need_close_cb ? MX_SIGNAL_SIGNALED : 0;
    mx_port_queue(md->ioport, &packet, sizeof(packet));

    // flag so we know to ignore further events
    handler->flags |= FLAG_DISCONNECTED;
}

static int mxio_dispatcher_thread(void* _md) {
    mxio_dispatcher_t* md = _md;
    mx_status_t r;

again:
    for (;;) {
        mx_io_packet_t packet;
        if ((r = mx_port_wait(md->ioport, &packet, sizeof(packet))) < 0) {
            printf("dispatcher: ioport wait failed %d\n", r);
            break;
        }
        handler_t* handler = (void*)(uintptr_t)packet.hdr.key;
        if (handler->flags & FLAG_DISCONNECTED) {
            // handler is awaiting gc
            // ignore events for it until we get the synthetic "destroy" event
            if (packet.hdr.type == MX_PORT_PKT_TYPE_USER) {
                destroy_handler(md, handler, packet.signals & MX_SIGNAL_SIGNALED);
            }
            continue;
        }
        if (packet.signals & MX_SIGNAL_READABLE) {
            if ((r = md->cb(handler->h, handler->cb, handler->cookie)) != 0) {
                if (r == ERR_DISPATCHER_NO_WORK) {
                    printf("mxio: dispatcher found no work to do!\n");
                } else {
                    disconnect_handler(md, handler, r < 0);
                    continue;
                }
            }
        }
        if (packet.signals & MX_SIGNAL_PEER_CLOSED) {
            // synthesize a close
            disconnect_handler(md, handler, true);
        }
    }

    printf("dispatcher: FATAL ERROR, EXITING\n");
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
    if ((md->ioport = mx_port_create(0u)) < 0) {
        free(md);
        return md->ioport;
    }
    md->cb = cb;
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

mx_status_t mxio_dispatcher_add(mxio_dispatcher_t* md, mx_handle_t h, void* cb, void* cookie) {
    handler_t* handler;
    mx_status_t r;

    if ((handler = malloc(sizeof(handler_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    handler->h = h;
    handler->flags = 0;
    handler->cb = cb;
    handler->cookie = cookie;

    mtx_lock(&md->lock);
    list_add_tail(&md->list, &handler->node);
    if ((r = mx_port_bind(md->ioport, (uint64_t)(uintptr_t)handler, h,
                             MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED)) < 0) {
        list_delete(&handler->node);
    }
    mtx_unlock(&md->lock);

    if (r < 0) {
        printf("dispatcher: failed to bind: %d\n", r);
        free(handler);
    }
    return r;
}
