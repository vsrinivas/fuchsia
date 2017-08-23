// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE

#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/boot/netboot.h>
#include <tftp/tftp.h>

#include "bootserver.h"

// Point to user-selected values (or NULL if no values selected)
uint16_t *tftp_block_size;
uint16_t *tftp_window_size;

typedef struct {
    int fd;
    const char* data;
    size_t datalen;
} xferdata;

void file_init(xferdata* xd) {
    xd->fd = -1;
    xd->data = NULL;
    xd->datalen = 0;
}

ssize_t file_open_read(const char* filename, void* cookie) {
    xferdata* xd = cookie;
    if (strcmp(filename, "(cmdline)")) {
        xd->fd = open(filename, O_RDONLY);
        if (xd->fd < 0) {
            fprintf(stderr, "%s: error: Could not open file %s\n", appname, filename);
            return TFTP_ERR_NOT_FOUND;
        }
        struct stat st;
        if (fstat(xd->fd, &st) < 0) {
            fprintf(stderr, "%s: error: Could not stat %s\n", appname, filename);
            goto err;
        }
        xd->datalen = st.st_size;
    }
    initialize_status(filename, xd->datalen);
    return xd->datalen;
err:
    if (xd->fd >= 0) {
        close(xd->fd);
        xd->fd = -1;
    }
    return TFTP_ERR_IO;
}

tftp_status file_read(void* data, size_t* length, off_t offset, void* cookie) {
    xferdata* xd = cookie;
    if (xd->fd < 0) {
        if ((offset > xd->datalen) || (offset + *length > xd->datalen)) {
            return TFTP_ERR_IO;
        }
        memcpy(data, &xd->data[offset], *length);
    } else {
        ssize_t bytes_read = pread(xd->fd, data, *length, offset);
        if (bytes_read < 0) {
            return TFTP_ERR_IO;
        }
        *length = bytes_read;
    }
    update_status(offset);
    return TFTP_NO_ERROR;
}

void file_close(void* cookie) {
    xferdata* xd = cookie;
    if (xd->fd >= 0) {
        close(xd->fd);
        xd->fd = -1;
    }
}

typedef struct {
    int socket;
    bool connected;
    uint32_t previous_timeout_ms;
    struct sockaddr_in6 target_addr;
} transport_state;

#define SEND_TIMEOUT_US 1000

int transport_send(void* data, size_t len, void* cookie) {
    transport_state* state = cookie;
    ssize_t send_result;
    do {
        struct pollfd poll_fds = {.fd = state->socket,
                                  .events = POLLOUT,
                                  .revents = 0};
        int poll_result = poll(&poll_fds, 1, SEND_TIMEOUT_US);
        if (poll_result < 0) {
            return TFTP_ERR_IO;
        }
        if (!state->connected) {
            state->target_addr.sin6_port = htons(NB_TFTP_INCOMING_PORT);
            send_result = sendto(state->socket, data, len, 0, (struct sockaddr*)&state->target_addr,
                                 sizeof(state->target_addr));
        } else {
            send_result = send(state->socket, data, len, 0);
        }
    } while ((send_result < 0) &&
             ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == ENOBUFS)));
    if (send_result < 0) {
        fprintf(stderr, "\n%s: Send failed with errno = %d\n", appname, (int)errno);
        return TFTP_ERR_IO;
    }
    return (int)send_result;
}

int transport_recv(void* data, size_t len, bool block, void* cookie) {
    transport_state* state = cookie;
    int flags = fcntl(state->socket, F_GETFL, 0);
    if (flags < 0) {
        return TFTP_ERR_IO;
    }
    int new_flags;
    if (block) {
        new_flags = flags & ~O_NONBLOCK;
    } else {
        new_flags = flags | O_NONBLOCK;
    }
    if ((new_flags != flags) && (fcntl(state->socket, F_SETFL, new_flags) != 0)) {
        return TFTP_ERR_IO;
    }
    ssize_t recv_result;
    struct sockaddr_in6 connection_addr;
    socklen_t addr_len = sizeof(connection_addr);
    if (!state->connected) {
        recv_result = recvfrom(state->socket, data, len, 0, (struct sockaddr*)&connection_addr,
                               &addr_len);
    } else {
        recv_result = recv(state->socket, data, len, 0);
    }
    if (recv_result < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return TFTP_ERR_TIMED_OUT;
        }
        return TFTP_ERR_INTERNAL;
    }
    if (!state->connected) {
        if (connect(state->socket, (struct sockaddr*)&connection_addr, sizeof(connection_addr)) <
            0) {
            return TFTP_ERR_IO;
        }
        memcpy(&state->target_addr, &connection_addr, sizeof(state->target_addr));
        state->connected = true;
    }
    return recv_result;
}

int transport_timeout_set(uint32_t timeout_ms, void* cookie) {
    transport_state* state = cookie;
    if (state->previous_timeout_ms != timeout_ms && timeout_ms > 0) {
        state->previous_timeout_ms = timeout_ms;
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = 1000 * (timeout_ms - 1000 * tv.tv_sec);
        return setsockopt(state->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    return 0;
}

int transport_init(transport_state* state, uint32_t timeout_ms, struct sockaddr_in6* addr) {
    state->socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (state->socket < 0) {
        fprintf(stderr, "%s: error: Cannot create socket %d\n", appname, errno);
        return -1;
    }
    state->previous_timeout_ms = 0;
    if (transport_timeout_set(timeout_ms, state) != 0) {
        fprintf(stderr, "%s: error: Unable to set socket timeout\n", appname);
        goto err;
    }
    state->connected = false;
    memcpy(&state->target_addr, addr, sizeof(struct sockaddr_in6));
    return 0;
err:
    close(state->socket);
    state->socket = -1;
    return -1;
}

#define INITIAL_CONNECTION_TIMEOUT 250
#define TFTP_BUF_SZ 2048

int tftp_xfer(struct sockaddr_in6* addr, const char* fn, const char* name) {
    int result = -1;
    xferdata xd;
    file_init(&xd);
    if (!strcmp(fn, "(cmdline)")) {
        xd.data = name;
        xd.datalen = strlen(name) + 1;
        name = use_filename_prefix ? NB_CMDLINE_FILENAME : "cmdline";
    }

    void* session_data = NULL;
    char* inbuf = NULL;
    char* outbuf = NULL;

    tftp_session* session = NULL;
    size_t session_data_sz = tftp_sizeof_session();

    if (!(session_data = calloc(session_data_sz, 1))  ||
        !(inbuf = malloc(TFTP_BUF_SZ)) ||
        !(outbuf = malloc(TFTP_BUF_SZ))) {
        fprintf(stderr, "%s: error: Unable to allocate memory\n", appname);
        goto done;
    }

    if (tftp_init(&session, session_data, session_data_sz) != TFTP_NO_ERROR) {
        fprintf(stderr, "%s: error: Unable to initialize tftp session\n", appname);
        goto done;
    }

    tftp_file_interface file_ifc = {file_open_read, NULL, file_read, NULL, file_close};
    tftp_session_set_file_interface(session, &file_ifc);

    transport_state ts;
    if (transport_init(&ts, INITIAL_CONNECTION_TIMEOUT, addr) < 0) {
        goto done;
    }
    tftp_transport_interface transport_ifc = {transport_send, transport_recv,
                                              transport_timeout_set};
    tftp_session_set_transport_interface(session, &transport_ifc);

    uint16_t default_block_size = DEFAULT_TFTP_BLOCK_SZ;
    uint16_t default_window_size = DEFAULT_TFTP_WIN_SZ;
    tftp_set_options(session, &default_block_size, NULL, &default_window_size);

    char err_msg[128];
    tftp_request_opts opts = {0};
    opts.inbuf_sz = TFTP_BUF_SZ;
    opts.inbuf = inbuf;
    opts.outbuf_sz = TFTP_BUF_SZ;
    opts.outbuf = outbuf;
    opts.err_msg = err_msg;
    opts.err_msg_sz = sizeof(err_msg);
    opts.block_size = tftp_block_size;
    opts.window_size = tftp_window_size;

    tftp_status status = tftp_push_file(session, &ts, &xd, fn, name, &opts);

    if (status < 0) {
        fprintf(stderr, "%s: %s (status = %d)\n", appname, opts.err_msg, (int)status);
        goto done;
    }

    result = 0;

done:
    if (session_data) {
        free(session_data);
    }
    if (inbuf) {
        free(inbuf);
    }
    if (outbuf) {
        free (outbuf);
    }
    file_close(&xd);
    return result;
}
