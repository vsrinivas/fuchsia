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
#include <magenta/syscalls.h>

#include "netsvc.h"

#define SCRATCHSZ 2048

typedef struct {
    bool is_write;
    char filename[PATH_MAX + 1];
    nbfile* netboot_file;
} file_info_t;

typedef struct {
    ip6_addr_t dest_addr;
    uint16_t dest_port;
    uint32_t timeout_ms;
} transport_info_t;

static char tftp_session_scratch[SCRATCHSZ];
char tftp_out_scratch[SCRATCHSZ];

static size_t last_msg_size = 0;
static tftp_session *session = NULL;
static file_info_t file_info;
static transport_info_t transport_info;

mx_time_t tftp_next_timeout = MX_TIME_INFINITE;

void file_init(file_info_t *file_info) {
    file_info->is_write = true;
    file_info->filename[0] = '\0';
    file_info->netboot_file = NULL;
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

    const size_t netboot_prefix_len = strlen(NB_FILENAME_PREFIX);
    if (netbootloader && !strncmp(filename, NB_FILENAME_PREFIX, netboot_prefix_len)) {
        // netboot
        file_info->netboot_file = netboot_get_buffer(filename, size);
        if (file_info->netboot_file != NULL) {
            return TFTP_NO_ERROR;
        }
    } else {
        // netcp
        if (netfile_open(filename, O_WRONLY) == 0) {
            return TFTP_NO_ERROR;
        }
    }
    return TFTP_ERR_INVALID_ARGS;
}

static tftp_status file_read(void* data, size_t* length, off_t offset, void* cookie) {
    printf("netsvc: tftp read file unimplemented\n");
    return TFTP_ERR_INTERNAL;
}

static tftp_status file_write(const void* data, size_t* length, off_t offset, void* cookie) {
    file_info_t* file_info = cookie;
    if (file_info->netboot_file != NULL) {
        nbfile* nb_file = file_info->netboot_file;
        if (((size_t)offset > nb_file->size) || (offset + *length) > nb_file->size) {
            return TFTP_ERR_INVALID_ARGS;
        }
        memcpy(nb_file->data + offset, data, *length);
        nb_file->offset = offset + *length;
        return TFTP_NO_ERROR;
    } else {
        int write_result = netfile_offset_write(data, offset, *length);
        if ((size_t) write_result == *length) {
            return TFTP_NO_ERROR;
        }
        if (write_result == -EBADF) {
            return TFTP_ERR_BAD_STATE;
        }
        return TFTP_ERR_IO;
    }
}

static void file_close(void* cookie) {
    file_info_t* file_info = cookie;
    if (file_info->netboot_file == NULL) {
        netfile_close();
    }
}

static int transport_send(void* data, size_t len, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    int bytes_sent = udp6_send(data, len, &transport_info->dest_addr,
                               transport_info->dest_port, NB_TFTP_OUTGOING_PORT);
    if (bytes_sent < 0) {
        return TFTP_ERR_IO;
    }

    // The timeout is relative to sending instead of receiving a packet, since there are some
    // received packets we want to ignore (duplicate ACKs).
    if (transport_info->timeout_ms != 0) {
        tftp_next_timeout = mx_deadline_after(MX_MSEC(transport_info->timeout_ms));
    }
    return bytes_sent;
}

static int transport_timeout_set(uint32_t timeout_ms, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    transport_info->timeout_ms = timeout_ms;
    return 0;
}

static void initialize_connection(const ip6_addr_t* saddr, uint16_t sport) {
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
    transport_info.timeout_ms = 1000;  // Reasonable default for now
    tftp_transport_interface transport_ifc = {transport_send, NULL, transport_timeout_set};
    tftp_session_set_transport_interface(session, &transport_ifc);
}

static void end_connection(void) {
    session = NULL;
    tftp_next_timeout = MX_TIME_INFINITE;
}

void tftp_timeout_expired(void) {
    tftp_status result = tftp_timeout(session, false, tftp_out_scratch, &last_msg_size,
                                      sizeof(tftp_out_scratch), &transport_info.timeout_ms,
                                      &file_info);
    if (result == TFTP_ERR_TIMED_OUT) {
        printf("netsvc: excessive timeouts, dropping tftp connection\n");
        end_connection();
        netfile_abort_write();
    } else if (result < 0) {
        printf("netsvc: failed to generate timeout response, dropping tftp connection\n");
        end_connection();
        netfile_abort_write();
    } else {
        if (last_msg_size > 0) {
            if (transport_send(tftp_out_scratch, last_msg_size, &transport_info) <
                (tftp_status) last_msg_size) {
                printf("netsvc: failed to send message\n");
            }
        }
    }
}

void tftp_recv(void* data, size_t len,
               const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {
    if (dport == NB_TFTP_INCOMING_PORT) {
        if (session != NULL) {
            printf("netsvc: only one simultaneous tftp session allowed\n");
            // ignore attempts to connect when a session is in progress
            return;
        }
        initialize_connection(saddr, sport);
    } else if (!session) {
        // Ignore anything sent to the outgoing port unless we've already
        // established a connection.
        return;
    }

    last_msg_size = sizeof(tftp_out_scratch);

    char err_msg[128];
    tftp_handler_opts handler_opts = { .inbuf = data,
                                       .inbuf_sz = len,
                                       .outbuf = tftp_out_scratch,
                                       .outbuf_sz = &last_msg_size,
                                       .err_msg = err_msg,
                                       .err_msg_sz = sizeof(err_msg) };
    tftp_status status = tftp_handle_msg(session, &transport_info, &file_info,
                                         &handler_opts);
    if (status < 0) {
        printf("netsvc: tftp protocol error:%s\n", err_msg);
        end_connection();
        netfile_abort_write();
    } else if (status == TFTP_TRANSFER_COMPLETED) {
        printf("netsvc: tftp %s of file %s completed\n",
               file_info.is_write ? "write" : "read",
               file_info.filename);
        end_connection();
    }
}
