// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is an example binary to exercise ulib/tftp. It runs on Linux or MacOs.

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "tftp/tftp.h"

#define BLOCKSZ 1024
#define WINSZ   64

#define DROPRATE 100

#define SCRATCHSZ 2048
static char scratch[SCRATCHSZ];
static char out_scratch[SCRATCHSZ];
static char in_scratch[SCRATCHSZ];

typedef struct connection connection_t;

struct tftp_file {
    int fd;
    size_t size;
};

struct connection {
    int socket;
    struct sockaddr_in out_addr;
    struct sockaddr_in in_addr;
    uint32_t previous_timeout_ms;
};

int connection_send(void* data, size_t len, void* transport_cookie) {
    connection_t* connection = (connection_t*)transport_cookie;
#if DROPRATE != 0
    if (rand() % DROPRATE == 0) {
        fprintf(stderr, "DROP\n");
        return len;
    }
#endif
    uint8_t* msg = data;
    uint16_t opcode = ntohs(*(uint16_t*)msg);
    fprintf(stderr, "sending opcode=%u\n", opcode);
    return sendto(connection->socket, data, len, 0, (struct sockaddr*)&connection->out_addr,
            sizeof(struct sockaddr_in));
}

int connection_receive(void* data, size_t len, bool block, void* transport_cookie) {
    connection_t* connection = (connection_t*)transport_cookie;
    socklen_t server_len;
    int fl = fcntl(connection->socket, F_GETFL, 0);
    if (fl < 0) {
        int e = errno;
        fprintf(stderr, "could not get socket flags: %d\n", errno);
        errno = e;
        return -1;
    }
    if (block) {
        fl &= ~O_NONBLOCK;
    } else {
        fl |= O_NONBLOCK;
    }
    int ret = fcntl(connection->socket, F_SETFL, fl);
    if (ret < 0) {
        int e = errno;
        fprintf(stderr, "could not set socket flags: %d\n", errno);
        errno = e;
        return -1;
    }
    ssize_t recv_result = recvfrom(connection->socket, data, len, 0,
                                   (struct sockaddr*)&connection->in_addr,
                                   &server_len);
    if (recv_result < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            return TFTP_ERR_TIMED_OUT;
        fprintf(stderr, "failed during recvfrom: errno=%d\n", (int) errno);
        return TFTP_ERR_INTERNAL;
    }
    return recv_result;
}

int connection_set_timeout(uint32_t timeout_ms, void* transport_cookie) {
    connection_t* connection = (connection_t*)transport_cookie;
    if (connection->previous_timeout_ms != timeout_ms && timeout_ms > 0) {
        fprintf(stdout, "Setting timeout to %dms\n", timeout_ms);
        connection->previous_timeout_ms = timeout_ms;
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = 1000 * (timeout_ms - 1000 * tv.tv_sec);
        return setsockopt(connection->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    return 0;
}

connection_t* create_connection(const char* hostname, int incoming_port, int outgoing_port) {
    connection_t* connection = (connection_t*)malloc(sizeof(connection_t));
    memset(connection, 0, sizeof(connection_t));

    struct hostent* server;

    if ((connection->socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "Cannot create socket\n");
        goto err;
    }

    if (!(server = gethostbyname(hostname))) {
        fprintf(stderr, "Could not resolve host '%s'\n", hostname);
        goto err;
    }

    memset(&connection->out_addr, 0, sizeof(struct sockaddr_in));
    connection->out_addr.sin_family = AF_INET;
    connection->out_addr.sin_port = htons(outgoing_port);
    void* server_addr = server->h_addr_list[0];
    memcpy(&connection->out_addr.sin_addr.s_addr, server_addr, server->h_length);

    memset(&connection->in_addr, 0, sizeof(struct sockaddr_in));
    connection->in_addr.sin_family = AF_INET;
    connection->in_addr.sin_port = htons(incoming_port);
    memcpy(&connection->in_addr.sin_addr.s_addr, server_addr, server->h_length);
    if (bind(connection->socket, (struct sockaddr*)&connection->in_addr, sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Could not bind\n");
        goto err;
    }

    connection->previous_timeout_ms = 0;
    return connection;

err:
    if (connection->socket) close(connection->socket);
    free(connection);
    return NULL;
}

void print_usage(void) {
    fprintf(stdout, "tftp (-s filename|-r)\n");
    fprintf(stdout, "\t -s filename to send the provided file\n");
    fprintf(stdout, "\t -r to receive a file\n");
}

ssize_t open_read_file(const char* filename, void* file_cookie) {
    fprintf(stdout, "Opening %s for reading\n", filename);
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "could not open file: err=%d\n", errno);
        return TFTP_ERR_IO;
    }
    struct tftp_file* f = (struct tftp_file*)file_cookie;
    f->fd = fd;
    struct stat st;
    if (fstat(f->fd, &st) < 0) {
        fprintf(stderr, "could not get file size: err=%d\n", errno);
        return TFTP_ERR_IO;
    }
    return st.st_size;
}

tftp_status open_write_file(const char* filename, size_t size, void* file_cookie) {
    fprintf(stdout, "Opening %s for writing\n", filename);
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "could not open file: err=%d\n", errno);
        return TFTP_ERR_IO;
    }
    struct tftp_file* f = (struct tftp_file*)file_cookie;
    f->fd = fd;
    return TFTP_NO_ERROR;
}

tftp_status read_file(void* data, size_t* length, off_t offset, void* file_cookie) {
    int fd = ((struct tftp_file*)file_cookie)->fd;
    ssize_t n = pread(fd, data, *length, offset);
    if (n < 0) {
        fprintf(stderr, "could not read file: offset %jd, err=%d\n", (intmax_t)offset, errno);
        return n;
    }
    *length = n;
    return TFTP_NO_ERROR;
}

tftp_status write_file(const void* data, size_t* length, off_t offset, void* file_cookie) {
    struct tftp_file* f = file_cookie;
    int fd = f->fd;
    ssize_t n = pwrite(fd, data, *length, offset);
    if (n < 0) {
        fprintf(stderr, "could not write file: offset %jd, err=%d\n", (intmax_t)offset, errno);
        return n;
    }
    *length = n;
    f->size = offset + *length;
    return TFTP_NO_ERROR;
}

void close_file(void* file_cookie) {
    struct tftp_file* f = file_cookie;
    close(f->fd);
}

int tftp_send_file_wrapper(tftp_session* session,
                           connection_t* connection,
                           const char* filename) {
    uint16_t block_size = BLOCKSZ;
    uint16_t window_size = WINSZ;
    tftp_set_options(session, &block_size, NULL, &window_size);

    struct tftp_file file_cookie;
    char err_msg[128];
    tftp_request_opts options = { 0 };
    options.inbuf = in_scratch;
    options.inbuf_sz = sizeof(in_scratch);
    options.outbuf = out_scratch;
    options.outbuf_sz = sizeof(out_scratch);
    options.err_msg = err_msg;
    options.err_msg_sz = sizeof(err_msg);
    tftp_status send_result =
        tftp_push_file(session, connection, &file_cookie, filename,
                       "magenta.bin", &options);
    if (send_result == TFTP_NO_ERROR) {
        return 0;
    }
    fprintf(stderr, "%s\n", err_msg);
    return -1;
}

int tftp_receive_file_wrapper(tftp_session* session,
                              connection_t* connection) {
    struct tftp_file file_cookie;
    char err_msg[128];
    tftp_handler_opts options = { 0 };
    options.inbuf = in_scratch;
    options.inbuf_sz = sizeof(in_scratch);
    options.outbuf = out_scratch;
    size_t outbuf_sz = sizeof(out_scratch);
    options.outbuf_sz = &outbuf_sz;
    options.err_msg = err_msg;
    options.err_msg_sz = sizeof(err_msg);

    tftp_status status;
    do {
        status = tftp_handle_request(session, connection, &file_cookie,
                                     &options);
    } while (status == TFTP_NO_ERROR || status == TFTP_ERR_TIMED_OUT);
    if (status == TFTP_TRANSFER_COMPLETED)
        return 0;
    fprintf(stderr, "%s\n", err_msg);
    return 1;
}

int main(int argc, char* argv[]) {
    const char* hostname = "127.0.0.1";
    int port = 2343;

    srand(time(NULL));

    if (argc < 2) {
        print_usage();
        return 1;
    }

    tftp_session* session = NULL;
    if (SCRATCHSZ < tftp_sizeof_session()) {
        fprintf(stderr, "Need more space for tftp session: %d < %zu\n",
                SCRATCHSZ, tftp_sizeof_session());
        return -1;
    }
    if (tftp_init(&session, scratch, SCRATCHSZ)) {
        fprintf(stderr, "Failed to initialize TFTP Session\n");
        return -1;
    }

    tftp_file_interface file_interface = {open_read_file,
                                          open_write_file,
                                          read_file,
                                          write_file,
                                          close_file};
    tftp_session_set_file_interface(session, &file_interface);
    tftp_transport_interface transport_interface = {connection_send,
                                                    connection_receive,
                                                    connection_set_timeout};
    tftp_session_set_transport_interface(session, &transport_interface);

    connection_t* connection;
    if (!strncmp(argv[1], "-s", 2)) {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        connection = create_connection(hostname, port, port + 1);
        if (!connection) {
            return -1;
        }
        return tftp_send_file_wrapper(session, connection, argv[2]);
    } else if (!strncmp(argv[1], "-r", 2)) {
        connection = create_connection(hostname, port + 1, port);
        if (!connection) {
            return -1;
        }
        return tftp_receive_file_wrapper(session, connection);
    } else {
        print_usage();
        return 2;
    }
    return 0;
}
