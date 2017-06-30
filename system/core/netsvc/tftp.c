// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <inet6/inet6.h>
#include <tftp/tftp.h>

#include <magenta/boot/netboot.h>

#include "netsvc.h"

#define SCRATCHSZ 2048
static char tftp_session_scratch[SCRATCHSZ];
char tftp_out_scratch[SCRATCHSZ];

typedef struct {
    bool is_write;
    char filename[PATH_MAX + 1];
} file_info_t;

void file_init(file_info_t *file_info) {
    file_info->is_write = true;
    file_info->filename[0] = '\0';
}

static ssize_t file_open_read(const char* filename, void* cookie) {
    printf("netsvc: tftp read file unimplemented\n");
    return TFTP_ERR_INTERNAL;
}

static tftp_status file_open_write(const char* filename, size_t size,
                                   void* cookie) {
    file_info_t* file_info = cookie;
    file_info->is_write = true;
    strncpy(file_info->filename, filename, PATH_MAX);
    file_info->filename[PATH_MAX] = '\0';

    if (netfile_open(filename, O_WRONLY) == 0)
        return TFTP_NO_ERROR;
    return TFTP_ERR_INVALID_ARGS;
}

static tftp_status file_read(void* data, size_t* length, off_t offset, void* cookie) {
    printf("netsvc: tftp read file unimplemented\n");
    return TFTP_ERR_INTERNAL;
}

static tftp_status file_write(const void* data, size_t* length, off_t offset, void* cookie) {
    int write_result = netfile_offset_write(data, offset, *length);
    if ((size_t) write_result == *length) {
        return TFTP_NO_ERROR;
    }
    if (write_result == -EBADF) {
        return TFTP_ERR_BAD_STATE;
    }
    return TFTP_ERR_IO;
}

static void file_close(void* cookie) {
    netfile_close();
}

typedef struct {
    ip6_addr_t dest_addr;
    uint16_t dest_port;
} transport_info_t;

static int transport_send(void* data, size_t len, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    int bytes_sent = udp6_send(data, len, &transport_info->dest_addr,
                               transport_info->dest_port, NB_TFTP_OUTGOING_PORT);
    return bytes_sent < 0 ? TFTP_ERR_IO : bytes_sent;
}

static int transport_timeout_set(uint32_t timeout_ms, void* transport_cookie) {
    // TODO
    return 0;
}

void tftp_recv(void* data, size_t len,
               const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {
    static tftp_session *session = NULL;
    static file_info_t file_info;
    static transport_info_t transport_info;

    if (dport == NB_TFTP_INCOMING_PORT) {
        if (session != NULL) {
            printf("netsvc: only one simultaneous tftp session allowed\n");
            // ignore attempts to connect when a session is in progress
            return;
        }
        // Start TFTP session
        int ret = tftp_init(&session, tftp_session_scratch,
                            sizeof(tftp_session_scratch));
        if (ret != TFTP_NO_ERROR) {
            printf("netsvc: failed to initiate tftp session\n");
            session = NULL;
            return;
        }

        // Initialize file interface
        file_init(&file_info);
        tftp_file_interface file_ifc = {file_open_read, file_open_write,
                                        file_read, file_write, file_close};
        tftp_session_set_file_interface(session, &file_ifc);

        // Initialize transport interface
        memcpy(&transport_info.dest_addr, saddr, sizeof(ip6_addr_t));
        transport_info.dest_port = sport;
        tftp_transport_interface transport_ifc = {transport_send, NULL, transport_timeout_set};
        tftp_session_set_transport_interface(session, &transport_ifc);
    } else if (!session) {
        // Ignore anything sent to the outgoing port unless we've already
        // established a connection.
        return;
    }

    size_t outlen = sizeof(tftp_out_scratch);

    char err_msg[128];
    tftp_handler_opts handler_opts = { .inbuf = data,
                                       .inbuf_sz = len,
                                       .outbuf = tftp_out_scratch,
                                       .outbuf_sz = outlen,
                                       .err_msg = err_msg,
                                       .err_msg_sz = sizeof(err_msg) };
    tftp_status status = tftp_handle_msg(session, &transport_info, &file_info,
                                         &handler_opts);
    if (status < 0) {
        printf("netsvc: tftp protocol error:%s\n", err_msg);
        session = NULL;
    } else if (status == TFTP_TRANSFER_COMPLETED) {
        printf("netsvc: tftp %s of file %s completed\n",
               file_info.is_write ? "write" : "read",
               file_info.filename);
        session = NULL;
    }
}

