// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <string.h>

#pragma GCC visibility pop

#define LOADER_SVC_MSG_MAX 1024
#define LOAD_OBJECT_FILE_PREFIX "lib/"

static mx_handle_t load_object(mx_handle_t log, struct bootfs* bootfs,
                               const char* name) {
    char file[LOADER_SVC_MSG_MAX - offsetof(mx_loader_svc_msg_t, data) +
              sizeof(LOAD_OBJECT_FILE_PREFIX)];
    memcpy(file, LOAD_OBJECT_FILE_PREFIX, sizeof(LOAD_OBJECT_FILE_PREFIX) - 1);
    memcpy(&file[sizeof(LOAD_OBJECT_FILE_PREFIX) - 1], name, strlen(name) + 1);
    return bootfs_open(log, "shared library", bootfs, file);
}

static bool handle_loader_rpc(mx_handle_t log, struct bootfs* bootfs,
                              mx_handle_t channel) {
    union {
        uint8_t buffer[1024];
        mx_loader_svc_msg_t msg;
    } msgbuf;
    const char* const string = (const char*)msgbuf.msg.data;

    uint32_t size;
    mx_status_t status = mx_channel_read(
        channel, 0, &msgbuf, NULL, sizeof(msgbuf), 0, &size, NULL);

    // This is the normal error for the other end going away,
    // which happens when the process dies.
    if (status == MX_ERR_PEER_CLOSED) {
        print(log, "loader-service channel peer closed on read\n", NULL);
        return false;
    }

    check(log, status, "mx_channel_read on loader-service channel failed\n");

    if (size < sizeof(msgbuf.msg))
        fail(log, MX_ERR_OUT_OF_RANGE,
             "loader-service request message too small\n");

    // Forcibly null-terminate the message data argument.
    msgbuf.buffer[sizeof(msgbuf) - 1] = 0;

    mx_handle_t handle = MX_HANDLE_INVALID;
    switch (msgbuf.msg.opcode) {
    case LOADER_SVC_OP_DONE:
        print(log, "loader-service received DONE request\n", NULL);
        return false;

    case LOADER_SVC_OP_DEBUG_PRINT:
        print(log, "loader-service: debug: ", string, "\n", NULL);
        break;

    case LOADER_SVC_OP_LOAD_OBJECT:
        handle = load_object(log, bootfs, string);
        break;

    case LOADER_SVC_OP_LOAD_SCRIPT_INTERP:
        fail(log, MX_ERR_INVALID_ARGS,
             "loader-service received LOAD_SCRIPT_INTERP request\n");
        break;

    default:
        fail(log, MX_ERR_INVALID_ARGS,
             "loader-service received invalid opcode\n");
        break;
    }

    // txid returned as received from the client.
    msgbuf.msg.opcode = LOADER_SVC_OP_STATUS;
    msgbuf.msg.arg = MX_OK;
    msgbuf.msg.reserved0 = 0;
    msgbuf.msg.reserved1 = 0;
    status = mx_channel_write(channel, 0, &msgbuf.msg, sizeof(msgbuf.msg),
                              &handle, handle == MX_HANDLE_INVALID ? 0 : 1);
    check(log, status, "mx_channel_write on loader-service channel failed\n");

    return true;
}

void loader_service(mx_handle_t log, struct bootfs* bootfs,
                    mx_handle_t channel) {
    print(log, "waiting for loader-service requests...\n", NULL);
    do {
        mx_signals_t signals;
        mx_status_t status = mx_object_wait_one(
            channel, MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
            MX_TIME_INFINITE, &signals);
        if (status == MX_ERR_BAD_STATE) {
            // This is the normal error for the other end going away,
            // which happens when the process dies.
            break;
        }
        check(log, status,
              "mx_object_wait_one failed on loader-service channel\n");
        if (signals & MX_CHANNEL_PEER_CLOSED) {
            print(log, "loader-service channel peer closed\n", NULL);
            break;
        }
        if (!(signals & MX_CHANNEL_READABLE)) {
            fail(log, MX_ERR_BAD_STATE,
                 "unexpected signal state on loader-service channel\n");
        }
    } while (handle_loader_rpc(log, bootfs, channel));

    check(log, mx_handle_close(channel),
          "mx_handle_close failed on loader-service channel\n");
}
