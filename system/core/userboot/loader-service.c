// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <string.h>

#pragma GCC visibility pop

#define LOADER_SVC_MSG_MAX 1024
#define LOAD_OBJECT_FILE_PREFIX "lib/"

struct loader_state {
    zx_handle_t log;
    struct bootfs* bootfs;
    char prefix[32];
    size_t prefix_len;
    bool exclusive;
};

static void loader_config(struct loader_state* state, const char* string) {
    size_t len = strlen(string);
    state->exclusive = false;
    if (string[len - 1] == '!') {
        --len;
        state->exclusive = true;
    }
    if (len >= sizeof(state->prefix) - 1) {
        fail(state->log, "loader-service config string too long");
    }
    memcpy(state->prefix, string, len);
    state->prefix[len++] = '/';
    state->prefix_len = len;
}

static zx_handle_t try_load_object(struct loader_state* state,
                                   const char* name, size_t prefix_len) {
    char file[LOADER_SVC_MSG_MAX - offsetof(zx_loader_svc_msg_t, data) +
              sizeof(LOAD_OBJECT_FILE_PREFIX) + prefix_len];
    memcpy(file, LOAD_OBJECT_FILE_PREFIX, sizeof(LOAD_OBJECT_FILE_PREFIX) - 1);
    memcpy(&file[sizeof(LOAD_OBJECT_FILE_PREFIX) - 1],
           state->prefix, prefix_len);
    memcpy(&file[sizeof(LOAD_OBJECT_FILE_PREFIX) - 1 + prefix_len],
           name, strlen(name) + 1);
    return bootfs_open(state->log, "shared library", state->bootfs, file);
}

static zx_handle_t load_object(struct loader_state* state, const char* name) {
    zx_handle_t vmo = try_load_object(state, name, state->prefix_len);
    if (vmo == ZX_HANDLE_INVALID && state->prefix_len > 0 && !state->exclusive)
        vmo = try_load_object(state, name, 0);
    if (vmo == ZX_HANDLE_INVALID)
        fail(state->log, "cannot find shared library '%s'", name);
    return vmo;
}

static bool handle_loader_rpc(struct loader_state* state,
                              zx_handle_t channel) {
    union {
        uint8_t buffer[1024];
        zx_loader_svc_msg_t msg;
    } msgbuf;
    zx_handle_t reqhandle;
    const char* const string = (const char*)msgbuf.msg.data;

    uint32_t size;
    uint32_t hcount;
    zx_status_t status = zx_channel_read(
        channel, 0, &msgbuf, &reqhandle, sizeof(msgbuf), 1, &size, &hcount);

    // This is the normal error for the other end going away,
    // which happens when the process dies.
    if (status == ZX_ERR_PEER_CLOSED) {
        printl(state->log, "loader-service channel peer closed on read");
        return false;
    }

    check(state->log, status,
          "zx_channel_read on loader-service channel failed");

    if (size < sizeof(msgbuf.msg))
        fail(state->log, "loader-service request message too small");

    // no opcodes which receive a handle are supported, but
    // we need to receive (and discard) the handle to politely
    // NAK clone requests
    if (hcount == 1)
        zx_handle_close(reqhandle);

    // Forcibly null-terminate the message data argument.
    msgbuf.buffer[sizeof(msgbuf) - 1] = 0;

    zx_handle_t handle = ZX_HANDLE_INVALID;
    switch (msgbuf.msg.opcode) {
    case LOADER_SVC_OP_DONE:
        printl(state->log, "loader-service received DONE request");
        return false;

    case LOADER_SVC_OP_DEBUG_PRINT:
        printl(state->log, "loader-service: debug: %s", string);
        break;

    case LOADER_SVC_OP_CONFIG:
        loader_config(state, string);
        break;

    case LOADER_SVC_OP_LOAD_OBJECT:
        handle = load_object(state, string);
        break;

    case LOADER_SVC_OP_CLONE:
        msgbuf.msg.arg = ZX_ERR_NOT_SUPPORTED;
        goto error_reply;

    case LOADER_SVC_OP_LOAD_SCRIPT_INTERP:
        fail(state->log, "loader-service received LOAD_SCRIPT_INTERP request");
        break;

    default:
        fail(state->log, "loader-service received invalid opcode");
        break;
    }

    // txid returned as received from the client.
    msgbuf.msg.arg = ZX_OK;
error_reply:
    msgbuf.msg.opcode = LOADER_SVC_OP_STATUS;
    msgbuf.msg.reserved0 = 0;
    msgbuf.msg.reserved1 = 0;
    status = zx_channel_write(channel, 0, &msgbuf.msg, sizeof(msgbuf.msg),
                              &handle, handle == ZX_HANDLE_INVALID ? 0 : 1);
    check(state->log, status,
          "zx_channel_write on loader-service channel failed");

    return true;
}

void loader_service(zx_handle_t log, struct bootfs* bootfs,
                    zx_handle_t channel) {
    printl(log, "waiting for loader-service requests...");

    struct loader_state state = {
        .log = log,
        .bootfs = bootfs,
    };

    do {
        zx_signals_t signals;
        zx_status_t status = zx_object_wait_one(
            channel, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
            ZX_TIME_INFINITE, &signals);
        if (status == ZX_ERR_BAD_STATE) {
            // This is the normal error for the other end going away,
            // which happens when the process dies.
            break;
        }
        check(log, status,
              "zx_object_wait_one failed on loader-service channel");
        if (signals & ZX_CHANNEL_PEER_CLOSED) {
            printl(log, "loader-service channel peer closed");
            break;
        }
        if (!(signals & ZX_CHANNEL_READABLE)) {
            fail(log, "unexpected signal state on loader-service channel");
        }
    } while (handle_loader_rpc(&state, channel));

    check(log, zx_handle_close(channel),
          "zx_handle_close failed on loader-service channel");
}
