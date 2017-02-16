// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <ddk/device.h>
#include <pty-core/pty-core.h>
#include <pty-core/pty-fifo.h>

#include <magenta/errors.h>
#include <magenta/device/pty.h>

#if 1
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) do {} while (0)
#endif

#define CTRL_(n) ((n) - 'A' + 1)

#define CTRL_C CTRL_('C')
#define CTRL_S CTRL_('S')
#define CTRL_Z CTRL_('Z')

#define ps_from_dev(d) containerof(d, pty_server_t, dev)
#define pc_from_dev(d) containerof(d, pty_client_t, dev)

#define PTY_CLI_RAW_MODE    (0x00000001u)

#define PTY_CLI_CONTROL     (0x00010000u)
#define PTY_CLI_ACTIVE      (0x00020000u)
#define PTY_CLI_PEER_CLOSED (0x00040000u)

struct pty_client {
    mx_device_t dev;
    pty_server_t* srv;
    uint32_t id;
    uint32_t flags;
    pty_fifo_t fifo;
    list_node_t node;
};

static mx_status_t pty_openat(pty_server_t* ps, mx_device_t** out, uint32_t id, uint32_t flags);



// pty client device operations

static ssize_t pty_client_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    pty_client_t* pc = pc_from_dev(dev);
    pty_server_t* ps = pc->srv;

    mtx_lock(&ps->lock);
    bool was_full = pty_fifo_is_full(&pc->fifo);
    size_t actual = pty_fifo_read(&pc->fifo, buf, count);
    if (pty_fifo_is_empty(&pc->fifo)) {
        device_state_clr(&pc->dev, DEV_STATE_READABLE);
    }
    if (was_full && actual) {
        device_state_set(&ps->dev, DEV_STATE_WRITABLE);
    }
    mtx_unlock(&ps->lock);

    if (actual > 0) {
        return actual;
    } else {
        return (pc->flags & PTY_CLI_PEER_CLOSED) ? ERR_PEER_CLOSED : ERR_SHOULD_WAIT;
    }
}

static ssize_t pty_client_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    pty_client_t* pc = pc_from_dev(dev);
    pty_server_t* ps = pc->srv;

    ssize_t r;

    mtx_lock(&ps->lock);
    if (pc->flags & PTY_CLI_ACTIVE) {
        size_t actual;
        r = ps->recv(ps, buf, count, &actual);
        if (r == NO_ERROR) {
            r = actual;
        } else if (r == ERR_SHOULD_WAIT) {
            device_state_clr(&pc->dev, DEV_STATE_WRITABLE);
        }
    } else {
        r = (pc->flags & PTY_CLI_PEER_CLOSED) ? ERR_PEER_CLOSED : ERR_SHOULD_WAIT;
    }
    mtx_unlock(&ps->lock);

    return r;
}

// mask of invalid features
#define PTY_FEATURE_BAD (~PTY_FEATURE_RAW)

static void pty_make_active_locked(pty_server_t* ps, pty_client_t* pc) {
    xprintf("pty cli %p (id=%u) becomes active\n", pc, pc->id);
    if (ps->active != pc) {
        if (ps->active) {
            ps->active->flags &= (~PTY_CLI_ACTIVE);
            device_state_clr(&ps->active->dev, DEV_STATE_WRITABLE);
        }
        ps->active = pc;
        pc->flags |= PTY_CLI_ACTIVE;
        device_state_set(&pc->dev, DEV_STATE_WRITABLE);
        if (pty_fifo_is_full(&pc->fifo)) {
            device_state_set_clr(&ps->dev, 0, DEV_STATE_WRITABLE | DEV_STATE_HANGUP);
        } else {
            device_state_set_clr(&ps->dev, DEV_STATE_WRITABLE, DEV_STATE_HANGUP);
        }
    }
}

static void pty_adjust_signals_locked(pty_client_t* pc) {
    uint32_t set = 0;
    uint32_t clr = 0;
    if (pc->flags & PTY_CLI_ACTIVE) {
        set = DEV_STATE_WRITABLE;
    } else {
        clr = DEV_STATE_WRITABLE;
    }
    if (pty_fifo_is_empty(&pc->fifo)) {
        clr = DEV_STATE_READABLE;
    } else {
        set = DEV_STATE_READABLE;
    }
    device_state_set_clr(&pc->dev, set, clr);
}


static ssize_t pty_client_ioctl(mx_device_t* dev, uint32_t op,
                                const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len) {
    pty_client_t* pc = pc_from_dev(dev);
    pty_server_t* ps = pc->srv;

    switch (op) {
    case IOCTL_PTY_CLR_SET_FEATURE: {
        const pty_clr_set_t* cs = in_buf;
        if ((in_len != sizeof(pty_clr_set_t)) ||
            (cs->clr & PTY_FEATURE_BAD) ||
            (cs->set & PTY_FEATURE_BAD)) {
            return ERR_INVALID_ARGS;
        }
        mtx_lock(&ps->lock);
        pc->flags = (pc->flags & (~cs->clr)) | cs->set;
        mtx_unlock(&ps->lock);
        return NO_ERROR;
    }
    case IOCTL_PTY_GET_WINDOW_SIZE: {
        pty_window_size_t* wsz = out_buf;
        if (out_len != sizeof(pty_window_size_t)) {
            return ERR_INVALID_ARGS;
        }
        mtx_lock(&ps->lock);
        wsz->width = ps->width;
        wsz->height = ps->height;
        mtx_unlock(&ps->lock);
        return sizeof(pty_window_size_t);
    }
    case IOCTL_PTY_MAKE_ACTIVE: {
        if (in_len != sizeof(uint32_t)) {
            return ERR_INVALID_ARGS;
        }
        if (!(pc->flags & PTY_CLI_CONTROL)) {
            return ERR_ACCESS_DENIED;
        }
        uint32_t id = *((uint32_t*)in_buf);
        mtx_lock(&ps->lock);
        pty_client_t* c;
        list_for_every_entry(&ps->clients, c, pty_client_t, node) {
            if (c->id == id) {
                pty_make_active_locked(ps, c);
                mtx_unlock(&ps->lock);
                return NO_ERROR;
            }
        }
        mtx_unlock(&ps->lock);
        return ERR_NOT_FOUND;
    }
    case IOCTL_PTY_READ_EVENTS: {
        if (!(pc->flags & PTY_CLI_CONTROL)) {
            return ERR_ACCESS_DENIED;
        }
        if (out_len != sizeof(uint32_t)) {
            return ERR_INVALID_ARGS;
        }
        mtx_lock(&ps->lock);
        uint32_t events = ps->events;
        ps->events = 0;
        if (ps->active == NULL) {
            events |= PTY_EVENT_HANGUP;
        }
        *((uint32_t*) out_buf) = events;
        device_state_clr(&pc->dev, PTY_SIGNAL_EVENT);
        mtx_unlock(&ps->lock);
        return sizeof(uint32_t);
    }
    default:
        if (ps->ioctl != NULL) {
            return ps->ioctl(ps, op, in_buf, in_len, out_buf, out_len);
        } else {
            return ERR_NOT_SUPPORTED;
        }
    }
}

static mx_status_t pty_client_release(mx_device_t* dev) {
    pty_client_t* pc = pc_from_dev(dev);
    pty_server_t* ps = pc->srv;

    mtx_lock(&ps->lock);

    // remove client from list of clients and downref server
    list_delete(&pc->node);
    pc->srv = NULL;
    int refcount = --ps->refcount;

    if (ps->control == pc) {
        ps->control = NULL;
    }
    if (ps->active == pc) {
        // signal controlling client as well, if there is one
        if (ps->control) {
            device_state_set(&ps->control->dev, PTY_SIGNAL_EVENT | DEV_STATE_HANGUP);
        }
        ps->active = NULL;
    }
    // signal server, if the last client has gone away
    if (list_is_empty(&ps->clients)) {
        device_state_set_clr(&ps->dev, DEV_STATE_HANGUP, DEV_STATE_WRITABLE);
    }
    mtx_unlock(&ps->lock);

    if (refcount == 0) {
        xprintf("pty srv %p release (from client)\n", ps);
        if (ps->release) {
            ps->release(ps);
        } else {
            free(ps);
        }
    }

    free(pc);
    xprintf("pty cli %p (id=%u) release\n", pc, pc->id);

    return NO_ERROR;
}

mx_status_t pty_client_openat(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    pty_client_t* pc = pc_from_dev(dev);
    pty_server_t* ps = pc->srv;
    uint32_t id = strtoul(path, NULL, 0);
    // only controlling clients may create additional clients
    if (!(pc->flags & PTY_CLI_CONTROL)) {
        return ERR_ACCESS_DENIED;
    }
    // clients may not create controlling clients
    if (id == 0) {
        return ERR_INVALID_ARGS;
    }
    return pty_openat(ps, out, id, flags);
}

mx_protocol_device_t pc_ops = {
    // .open = default, allow cloning
    .openat = pty_client_openat,
    .release = pty_client_release,
    .read = pty_client_read,
    .write = pty_client_write,
    .ioctl = pty_client_ioctl,
};

// used by both client and server ptys to create new client ptys

static mx_status_t pty_openat(pty_server_t* ps, mx_device_t** out, uint32_t id, uint32_t flags) {
    pty_client_t* pc;
    if ((pc = calloc(1, sizeof(pty_client_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    pc->id = id;
    pc->flags = 0;
    pc->fifo.head = 0;
    pc->fifo.tail = 0;
    device_init(&pc->dev, NULL, "pty", &pc_ops);

    mtx_lock(&ps->lock);
    // require that client ID is unique
    pty_client_t* c;
    list_for_every_entry(&ps->clients, c, pty_client_t, node) {
        if (c->id == id) {
            mtx_unlock(&ps->lock);
            free(pc);
            return ERR_INVALID_ARGS;
        }
    }

    // success: add new client to clients list, etc
    list_add_tail(&ps->clients, &pc->node);
    pc->srv = ps;
    ps->refcount++;
    if (ps->active == NULL) {
        pty_make_active_locked(ps, pc);
    }
    if (id == 0) {
        ps->control = pc;
        pc->flags |= PTY_CLI_CONTROL;
    }
    mtx_unlock(&ps->lock);

    xprintf("pty cli %p (id=%u) created (srv %p)\n", pc, pc->id, ps);

    mx_status_t status = device_add_instance(&pc->dev, &ps->dev);
    if (status < 0) {
        pty_client_release(&pc->dev);
        return status;
    }

    mtx_lock(&ps->lock);
    pty_adjust_signals_locked(pc);
    mtx_unlock(&ps->lock);

    *out = &pc->dev;
    return NO_ERROR;
}


// pty server device operations

void pty_server_resume_locked(pty_server_t* ps) {
    if (ps->active) {
        device_state_set(&ps->active->dev, DEV_STATE_WRITABLE);
    }
}

mx_status_t pty_server_send(pty_server_t* ps, const void* data, size_t len, bool atomic, size_t* actual) {
    //TODO: rw signals
    mx_status_t status;
    mtx_lock(&ps->lock);
    if (ps->active) {
        pty_client_t* pc = ps->active;
        bool was_empty = pty_fifo_is_empty(&pc->fifo);
        if (atomic || (pc->flags & PTY_CLI_RAW_MODE)) {
            *actual = pty_fifo_write(&pc->fifo, data, len, atomic);
        } else {
            if (len > PTY_FIFO_SIZE) {
                len = PTY_FIFO_SIZE;
            }
            const uint8_t *ch = data;
            unsigned n = 0;
            unsigned evt = 0;
            while (n < len) {
                if (*ch++ == CTRL_C) {
                    evt = PTY_EVENT_INTERRUPT;
                    break;
                }
                n++;
            }
            size_t r = pty_fifo_write(&pc->fifo, data, n, false);
            if ((r == n) && evt) {
                // consume the event
                r++;
                ps->events |= evt;
                xprintf("pty cli %p evt %x\n", pc, evt);
                if (ps->control) {
                    device_state_set(&ps->control->dev, PTY_SIGNAL_EVENT);
                }
            }
            *actual = r;
        }
        if (was_empty && *actual) {
            device_state_set(&pc->dev, DEV_STATE_READABLE);
        }
        if (pty_fifo_is_full(&pc->fifo)) {
            device_state_clr(&ps->dev, DEV_STATE_WRITABLE);
        }
        status = NO_ERROR;
    } else {
        *actual = 0;
        status = ERR_PEER_CLOSED;
    }
    mtx_unlock(&ps->lock);
    return status;
}

void pty_server_set_window_size(pty_server_t* ps, uint32_t w, uint32_t h) {
    mtx_lock(&ps->lock);
    ps->width = w;
    ps->height = h;
    //TODO signal?
    mtx_unlock(&ps->lock);
}

mx_status_t pty_server_openat(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    pty_server_t* ps = ps_from_dev(dev);
    uint32_t id = strtoul(path, NULL, 0);
    return pty_openat(ps, out, id, flags);
}

mx_status_t pty_server_release(mx_device_t* dev) {
    pty_server_t* ps = ps_from_dev(dev);

    mtx_lock(&ps->lock);
    // inform clients that server is gone
    pty_client_t* pc;
    list_for_every_entry(&ps->clients, pc, pty_client_t, node) {
        pc->flags = (pc->flags & (~PTY_CLI_ACTIVE)) | PTY_CLI_PEER_CLOSED;
        device_state_set(&pc->dev, DEV_STATE_HANGUP);
    }
    int32_t refcount = --ps->refcount;
    mtx_unlock(&ps->lock);

    if (refcount == 0) {
        xprintf("pty srv %p release (from server)\n", ps);
        if (ps->release) {
            ps->release(ps);
        } else {
            free(ps);
        }
    }

    return NO_ERROR;
}

void pty_server_init(pty_server_t* ps) {
    mtx_init(&ps->lock, mtx_plain);
    ps->refcount = 1;
    list_initialize(&ps->clients);
    ps->active = NULL;
    ps->control = NULL;
    ps->events = 0;
    ps->width = 0;
    ps->height = 0;
}
