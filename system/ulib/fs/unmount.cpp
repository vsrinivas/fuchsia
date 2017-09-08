// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs.h>
#include <mxio/debug.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Sends an 'unmount' signal on the srv handle, and waits until it is closed.
// Consumes 'srv'.
mx_status_t vfs_unmount_handle(mx_handle_t srv, mx_time_t deadline) {
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);

    // the only other messages we ever send are no-reply OPEN or CLONE with
    // txid of 0.
    msg.txid = 1;
    msg.op = MXRIO_IOCTL;
    msg.arg2.op = IOCTL_VFS_UNMOUNT_FS;

    mx_channel_call_args_t args;
    args.wr_bytes = &msg;
    args.wr_handles = NULL;
    args.rd_bytes = &msg;
    args.rd_handles = NULL;
    args.wr_num_bytes = MXRIO_HDR_SZ;
    args.wr_num_handles = 0;
    args.rd_num_bytes = MXRIO_HDR_SZ + MXIO_CHUNK_SIZE;
    args.rd_num_handles = 0;

    uint32_t dsize;
    uint32_t hcount;
    mx_status_t rs;

    // At the moment, we don't actually care what the response is from the
    // filesystem server (or even if it supports the unmount operation). As
    // soon as ANY response comes back, either in the form of a closed handle
    // or a visible response, shut down.
    mx_status_t status = mx_channel_call(srv, 0, deadline, &args, &dsize, &hcount, &rs);
    if (status == MX_ERR_CALL_FAILED) {
        // Write phase succeeded. The target filesystem had a chance to unmount properly.
        status = MX_OK;
    } else if (status == MX_OK) {
        // Read phase succeeded. If the target filesystem returned an error, we
        // should parse it.
        if (dsize < MXRIO_HDR_SZ) {
            status = MX_ERR_IO;
        } else {
            status = msg.arg;
        }
    }
    mx_handle_close(srv);
    return status;
}
