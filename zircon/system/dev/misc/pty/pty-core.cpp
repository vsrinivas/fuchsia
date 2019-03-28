// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <utility>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/auto_lock.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/pty/c/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>

#include "pty-core.h"
#include "pty-fifo.h"

#define CTRL_(n) ((n) - 'A' + 1)

#define CTRL_C CTRL_('C')
#define CTRL_S CTRL_('S')
#define CTRL_Z CTRL_('Z')

// clang-format off
#define PTY_CLI_RAW_MODE    (0x00000001u)
#define PTY_CLI_CONTROL     (0x00010000u)
#define PTY_CLI_ACTIVE      (0x00020000u)
#define PTY_CLI_PEER_CLOSED (0x00040000u)
// clang-format on

struct pty_client {
    zx_device_t* zxdev;
    pty_server_t* srv;
    uint32_t id;
    uint32_t flags;
    pty_fifo_t fifo;
    list_node_t node;
};

static zx_status_t pty_open_client(pty_server_t* ps, uint32_t id, zx::channel channel,
                                   zx_device_t** out);

// pty client device operations

static zx_status_t pty_client_read(void* ctx, void* buf, size_t count, zx_off_t off,
                                   size_t* actual) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    zxlogf(TRACE, "PTY Client %p (id=%u) read\n", pc, pc->id);

    mtx_lock(&ps->lock);
    bool was_full = pty_fifo_is_full(&pc->fifo);
    size_t length = pty_fifo_read(&pc->fifo, buf, count);
    if (pty_fifo_is_empty(&pc->fifo)) {
        device_state_clr(pc->zxdev, DEV_STATE_READABLE);
    }
    if (was_full && length) {
        device_state_set(ps->zxdev, DEV_STATE_WRITABLE);
    }
    mtx_unlock(&ps->lock);

    if (length > 0) {
        *actual = length;
        return ZX_OK;
    } else {
        return (pc->flags & PTY_CLI_PEER_CLOSED) ? ZX_ERR_PEER_CLOSED : ZX_ERR_SHOULD_WAIT;
    }
}

static zx_status_t pty_client_write_chunk_locked(pty_client_t* pc, pty_server_t* ps,
                                                 const void* buf, size_t count,
                                                 size_t* actual) {
    size_t length;

    zx_status_t status = ps->recv(ps, buf, count, &length);
    if (status == ZX_OK) {
        *actual = length;
    } else if (status == ZX_ERR_SHOULD_WAIT) {
        device_state_clr(pc->zxdev, DEV_STATE_WRITABLE);
    }

    return status;
}

static zx_status_t pty_client_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                                    size_t* actual) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    zxlogf(TRACE, "PTY Client %p (id=%u) write\n", pc, pc->id);

    zx_status_t status;

    if (count == 0) {
        *actual = 0;
        return ZX_OK;
    }

    fbl::AutoLock lock(&ps->lock);

    if (!(pc->flags & PTY_CLI_ACTIVE)) {
        return (pc->flags & PTY_CLI_PEER_CLOSED) ? ZX_ERR_PEER_CLOSED : ZX_ERR_SHOULD_WAIT;
    }

    if (pc->flags & fuchsia_hardware_pty_FEATURE_RAW) {
        return pty_client_write_chunk_locked(pc, ps, buf, count, actual);
    }

    // newline translation time
    auto chunk_start = static_cast<const char*>(buf);
    auto chunk_end = chunk_start;
    size_t chunk_length;
    size_t chunk_actual;
    size_t sent = 0;

    auto partial_result = [&sent, actual](zx_status_t status) {
        if (sent) {
            *actual = sent;
            return ZX_OK;
        }
        return status;
    };

    for (size_t i = 0; i < count; i++) {
        // just iterate until there's a linefeed character
        if (*chunk_end != '\n') {
            chunk_end++;
            continue;
        }

        // send up to (but not including) the linefeed
        chunk_length = chunk_end - chunk_start;
        status = pty_client_write_chunk_locked(pc, ps, chunk_start, chunk_length, &chunk_actual);
        if (status != ZX_OK) {
            return partial_result(status);
        }

        sent += chunk_actual;
        if (chunk_actual != chunk_length) {
            return partial_result(status);
        }

        // send the line ending
        status = pty_client_write_chunk_locked(pc, ps, "\r\n", 2, &chunk_actual);
        if (status != ZX_OK) {
            return partial_result(status);
        }

        // this case means only the \r of the \r\n was sent; report to the caller
        // as if it didn't work at all
        if (chunk_actual != 2) {
            return partial_result(status);
        }

        // don't increment for the \r
        sent++;

        chunk_start = chunk_end + 1;
        chunk_end = chunk_start;
    }

    // finish up the buffer if necessary
    chunk_length = chunk_end - chunk_start;
    status = pty_client_write_chunk_locked(pc, ps, chunk_start, chunk_length, &chunk_actual);
    if (status == ZX_OK) {
        sent += chunk_actual;
    }

    return partial_result(status);
}

static void pty_make_active_locked(pty_server_t* ps, pty_client_t* pc) {
    zxlogf(TRACE, "PTY Client %p (id=%u) becomes active\n", pc, pc->id);
    if (ps->active != pc) {
        if (ps->active) {
            ps->active->flags &= (~PTY_CLI_ACTIVE);
            device_state_clr(ps->active->zxdev, DEV_STATE_WRITABLE);
        }
        ps->active = pc;
        pc->flags |= PTY_CLI_ACTIVE;
        device_state_set(pc->zxdev, DEV_STATE_WRITABLE);
        if (pty_fifo_is_full(&pc->fifo)) {
            device_state_clr_set(ps->zxdev, DEV_STATE_WRITABLE | DEV_STATE_HANGUP, 0);
        } else {
            device_state_clr_set(ps->zxdev, DEV_STATE_HANGUP, DEV_STATE_WRITABLE);
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
    device_state_clr_set(pc->zxdev, clr, set);
}

static void pty_client_release(void* ctx) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    zxlogf(TRACE, "PTY Client %p (id=%u) release\n", pc, pc->id);

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
            device_state_set(ps->control->zxdev,
                             fuchsia_hardware_pty_SIGNAL_EVENT | DEV_STATE_HANGUP);
        }
        ps->active = NULL;
    }
    // signal server, if the last client has gone away
    if (list_is_empty(&ps->clients)) {
        device_state_clr_set(ps->zxdev, DEV_STATE_WRITABLE, DEV_STATE_READABLE | DEV_STATE_HANGUP);
    }
    mtx_unlock(&ps->lock);

    if (refcount == 0) {
        zxlogf(TRACE, "PTY Server %p release (from client)\n", ps);
        if (ps->release) {
            ps->release(ps);
        } else {
            free(ps);
        }
    }

    free(pc);
}

zx_status_t pty_client_openat(void* ctx, zx_device_t** out, const char* path, uint32_t flags) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    uint32_t id = static_cast<uint32_t>(strtoul(path, NULL, 0));
    zxlogf(TRACE, "PTY Client %p (id=%u) openat %u\n", pc, pc->id, id);
    // only controlling clients may create additional clients
    if (!(pc->flags & PTY_CLI_CONTROL)) {
        return ZX_ERR_ACCESS_DENIED;
    }
    // clients may not create controlling clients
    if (id == 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pty_open_client(ps, id, zx::channel(), out);
}

zx_status_t pty_client_fidl_OpenClient(void* ctx, uint32_t id, zx_handle_t handle,
                                       fidl_txn_t* txn) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    zx::channel channel(handle);
    zxlogf(TRACE, "PTY Client %p (id=%u) openat %u\n", pc, pc->id, id);
    // only controlling clients may create additional clients
    if (!(pc->flags & PTY_CLI_CONTROL)) {
        return ZX_ERR_ACCESS_DENIED;
    }
    // clients may not create controlling clients
    if (id == 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status = pty_open_client(ps, id, std::move(channel), nullptr);
    return fuchsia_hardware_pty_DeviceOpenClient_reply(txn, status);
}

#define fuchsia_hardware_pty_FEATURE_BAD (~fuchsia_hardware_pty_FEATURE_RAW)

zx_status_t pty_client_ClrSetFeature(void* ctx, uint32_t clr, uint32_t set, fidl_txn_t* txn) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    zx_status_t status = ZX_OK;

    zxlogf(TRACE, "PTY Client %p (id=%u) message: clear and/or set feature\n", pc, pc->id);
    if ((clr & fuchsia_hardware_pty_FEATURE_BAD) ||
        (set & fuchsia_hardware_pty_FEATURE_BAD)) {
        status = ZX_ERR_NOT_SUPPORTED;
    } else {
        fbl::AutoLock(&ps->lock);
        pc->flags = (pc->flags & ~clr) | set;
    }
    return fuchsia_hardware_pty_DeviceClrSetFeature_reply(txn, status, pc->flags);
}

zx_status_t pty_client_GetWindowSize(void* ctx, fidl_txn_t* txn) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);

    zxlogf(TRACE, "PTY Client %p (id=%u) message: get window size\n", pc, pc->id);
    mtx_lock(&ps->lock);
    fuchsia_hardware_pty_WindowSize wsz;
    wsz.width = ps->width;
    wsz.height = ps->height;
    mtx_unlock(&ps->lock);

    return fuchsia_hardware_pty_DeviceGetWindowSize_reply(txn, ZX_OK, &wsz);
}

zx_status_t pty_client_MakeActive(void* ctx, uint32_t client_pty_id, fidl_txn_t* txn) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);

    zxlogf(TRACE, "PTY Client %p (id=%u) message: make active\n", pc, pc->id);

    if (!(pc->flags & PTY_CLI_CONTROL)) {
        return fuchsia_hardware_pty_DeviceMakeActive_reply(txn, ZX_ERR_ACCESS_DENIED);
    }
    mtx_lock(&ps->lock);
    pty_client_t* c;
    list_for_every_entry (&ps->clients, c, pty_client_t, node) {
        if (c->id == client_pty_id) {
            pty_make_active_locked(ps, c);
            mtx_unlock(&ps->lock);
            return fuchsia_hardware_pty_DeviceMakeActive_reply(txn, ZX_OK);
        }
    }
    mtx_unlock(&ps->lock);
    return fuchsia_hardware_pty_DeviceMakeActive_reply(txn, ZX_ERR_NOT_FOUND);
}

zx_status_t pty_client_ReadEvents(void* ctx, fidl_txn_t* txn) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    uint32_t events = 0;

    zxlogf(TRACE, "PTY Client %p (id=%u) message: read events\n", pc, pc->id);
    if (!(pc->flags & PTY_CLI_CONTROL)) {
        return fuchsia_hardware_pty_DeviceReadEvents_reply(txn, ZX_ERR_ACCESS_DENIED, events);
    }
    mtx_lock(&ps->lock);
    events = ps->events;
    ps->events = 0;
    if (ps->active == NULL) {
        events |= fuchsia_hardware_pty_EVENT_HANGUP;
    }
    device_state_clr(pc->zxdev, fuchsia_hardware_pty_SIGNAL_EVENT);
    mtx_unlock(&ps->lock);
    return fuchsia_hardware_pty_DeviceReadEvents_reply(txn, ZX_OK, events);
}

zx_status_t pty_client_SetWindowSize(void* ctx, const fuchsia_hardware_pty_WindowSize* size,
                                     fidl_txn_t* txn) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);

    if (ps->set_window_size != NULL) {
        return ps->set_window_size(ps, size, txn);
    } else {
        return fuchsia_hardware_pty_DeviceSetWindowSize_reply(txn, ZX_ERR_NOT_SUPPORTED);
    }
}

static fuchsia_hardware_pty_Device_ops_t fidl_ops = {
    .OpenClient = pty_client_fidl_OpenClient,
    .ClrSetFeature = pty_client_ClrSetFeature,
    .GetWindowSize = pty_client_GetWindowSize,
    .MakeActive = pty_client_MakeActive,
    .ReadEvents = pty_client_ReadEvents,
    .SetWindowSize = pty_client_SetWindowSize
};

zx_status_t pty_client_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

zx_protocol_device_t pc_ops = []() {
    zx_protocol_device_t ops = {};
    ops.version = DEVICE_OPS_VERSION;
    // ops.open = default, allow cloning
    ops.open_at = pty_client_openat;
    ops.release = pty_client_release;
    ops.read = pty_client_read;
    ops.write = pty_client_write;
    ops.message = pty_client_message;
    return ops;
}();

static zx_status_t pty_open_client(pty_server_t* ps, uint32_t id, zx::channel channel,
                                   zx_device_t** out) {
    auto pc = static_cast<pty_client_t*>(calloc(1, sizeof(pty_client_t)));
    if (!pc) {
        return ZX_ERR_NO_MEMORY;
    }

    pc->id = id;
    pc->flags = 0;
    pc->fifo.head = 0;
    pc->fifo.tail = 0;
    zx_status_t status;

    unsigned num_clients = 0;
    mtx_lock(&ps->lock);
    // require that client ID is unique
    pty_client_t* c;
    list_for_every_entry (&ps->clients, c, pty_client_t, node) {
        if (c->id == id) {
            mtx_unlock(&ps->lock);
            free(pc);
            return ZX_ERR_INVALID_ARGS;
        }
        num_clients++;
    }
    list_add_tail(&ps->clients, &pc->node);

    ps->refcount++;
    mtx_unlock(&ps->lock);

    pc->srv = ps;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "pty";
    args.ctx = pc;
    args.ops = &pc_ops;
    args.flags = DEVICE_ADD_INSTANCE;
    if (channel) {
        args.client_remote = channel.release();
    }

    status = device_add(ps->zxdev, &args, &pc->zxdev);
    if (status < 0) {
        pty_client_release(pc->zxdev);
        return status;
    }

    if (ps->active == NULL) {
        pty_make_active_locked(ps, pc);
    }
    if (id == 0) {
        ps->control = pc;
        pc->flags |= PTY_CLI_CONTROL;
    }

    zxlogf(TRACE, "PTY Client %p (id=%u) created (server %p)\n", pc, pc->id, ps);

    mtx_lock(&ps->lock);
    if (num_clients == 0) {
        // if there were no clients, make sure we take server
        // out of HANGUP and READABLE, where it landed if all
        // its clients had closed
        device_state_clr(ps->zxdev, DEV_STATE_READABLE | DEV_STATE_HANGUP);
    }
    pty_adjust_signals_locked(pc);
    mtx_unlock(&ps->lock);

    if (out) {
        *out = pc->zxdev;
    }
    return ZX_OK;
}

// pty server device operations

void pty_server_resume_locked(pty_server_t* ps) {
    if (ps->active) {
        device_state_set(ps->active->zxdev, DEV_STATE_WRITABLE);
    }
}

zx_status_t pty_server_send(pty_server_t* ps, const void* data, size_t len, bool atomic,
                            size_t* actual) {
    // TODO: rw signals
    zxlogf(TRACE, "PTY Server %p send\n", ps);
    zx_status_t status;
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
            auto ch = static_cast<const uint8_t*>(data);
            unsigned n = 0;
            unsigned evt = 0;
            while (n < len) {
                if (*ch++ == CTRL_C) {
                    evt = fuchsia_hardware_pty_EVENT_INTERRUPT;
                    break;
                }
                n++;
            }
            size_t r = pty_fifo_write(&pc->fifo, data, n, false);
            if ((r == n) && evt) {
                // consume the event
                r++;
                ps->events |= evt;
                zxlogf(TRACE, "PTY Client %p event %x\n", ps, evt);
                if (ps->control) {
                    static_assert(fuchsia_hardware_pty_SIGNAL_EVENT == DEV_STATE_OOB);
                    device_state_set(ps->control->zxdev, fuchsia_hardware_pty_SIGNAL_EVENT);
                }
            }
            *actual = r;
        }
        if (was_empty && *actual) {
            device_state_set(pc->zxdev, DEV_STATE_READABLE);
        }
        if (pty_fifo_is_full(&pc->fifo)) {
            device_state_clr(ps->zxdev, DEV_STATE_WRITABLE);
        }
        status = ZX_OK;
    } else {
        *actual = 0;
        status = ZX_ERR_PEER_CLOSED;
    }
    mtx_unlock(&ps->lock);
    return status;
}

void pty_server_set_window_size(pty_server_t* ps, uint32_t w, uint32_t h) {
    zxlogf(TRACE, "PTY Server %p set window size %ux%u\n", ps, w, h);
    mtx_lock(&ps->lock);
    ps->width = w;
    ps->height = h;
    // TODO signal?
    mtx_unlock(&ps->lock);
}

zx_status_t pty_server_openat(void* ctx, zx_device_t** out, const char* path, uint32_t flags) {
    auto ps = static_cast<pty_server_t*>(ctx);
    uint32_t id = static_cast<uint32_t>(strtoul(path, NULL, 0));
    zxlogf(TRACE, "PTY Server %p openat %u\n", ps, id);
    return pty_open_client(ps, id, zx::channel(), out);
}

zx_status_t pty_server_fidl_OpenClient(void* ctx, uint32_t id, zx_handle_t handle,
                                       fidl_txn_t* txn) {
    auto ps = static_cast<pty_server_t*>(ctx);
    zx::channel channel(handle);
    zxlogf(TRACE, "PTY Server %p OpenClient %u\n", ps, id);
    zx_status_t status = pty_open_client(ps, id, std::move(channel), nullptr);
    return fuchsia_hardware_pty_DeviceOpenClient_reply(txn, status);
}

void pty_server_release(void* ctx) {
    auto ps = static_cast<pty_server_t*>(ctx);

    mtx_lock(&ps->lock);
    // inform clients that server is gone
    pty_client_t* pc;
    list_for_every_entry (&ps->clients, pc, pty_client_t, node) {
        pc->flags = (pc->flags & (~PTY_CLI_ACTIVE)) | PTY_CLI_PEER_CLOSED;
        device_state_set(pc->zxdev, DEV_STATE_HANGUP);
    }
    int32_t refcount = --ps->refcount;
    mtx_unlock(&ps->lock);

    if (refcount == 0) {
        zxlogf(TRACE, "PTY Server %p release (from server)\n", ps);
        if (ps->release) {
            ps->release(ps);
        } else {
            free(ps);
        }
    }
}

void pty_server_init(pty_server_t* ps) {
    zxlogf(TRACE, "PTY Server %p init\n", ps);
    mtx_init(&ps->lock, mtx_plain);
    ps->refcount = 1;
    list_initialize(&ps->clients);
    ps->active = NULL;
    ps->control = NULL;
    ps->events = 0;
    ps->width = 0;
    ps->height = 0;
}
