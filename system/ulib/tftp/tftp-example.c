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
#define SCRATCHSZ 2048

#define DROPRATE 20

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

int connection_send(connection_t* connection, void* data, size_t len) {
    if (rand() % DROPRATE == 0) {
        fprintf(stderr, "DROP\n");
        return len;
    }
    uint8_t* msg = data;
    uint16_t opcode = ntohs(*(uint16_t*)msg);
    fprintf(stderr, "sending opcode=%u\n", opcode);
    return sendto(connection->socket, data, len, 0, (struct sockaddr*)&connection->out_addr,
            sizeof(struct sockaddr_in));
}

int connection_receive(connection_t* connection, void* data, size_t len, bool block) {
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
    return recvfrom(connection->socket, data, len, 0, (struct sockaddr*)&connection->in_addr, &server_len);
}

int connection_set_timeout(connection_t* connection, uint32_t timeout_ms) {
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

void print_usage() {
    fprintf(stdout, "tftp (-s filename|-r filename)\n");
    fprintf(stdout, "\t -s filename to send the provided file\n");
    fprintf(stdout, "\t -r filename to receive a file\n");
}

tftp_status receive_open_file(const char* filename,
                              size_t size,
                              void* cookie) {
    fprintf(stdout, "Opening %s\n", filename);
    int fd = open(filename, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "could not open file: err=%d\n", errno);
        return fd;
    }
    struct tftp_file* f = cookie;
    f->fd = fd;
    return TFTP_NO_ERROR;
}

tftp_status read_file(void* data, size_t* length, off_t offset, void* cookie) {
    int fd = ((struct tftp_file*)cookie)->fd;
    ssize_t n = pread(fd, data, *length, offset);
    if (n < 0) {
        fprintf(stderr, "could not read file: offset %jd, err=%d\n", (intmax_t)offset, errno);
        return n;
    }
    *length = n;
    return TFTP_NO_ERROR;
}

tftp_status write_file(const void* data, size_t* length, off_t offset, void* cookie) {
    struct tftp_file* f = cookie;
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

tftp_status tftp_send_file(tftp_session* session,
                           const char* hostname,
                           int incoming_port,
                           int outgoing_port,
                           const char* filename) {
    connection_t* connection = create_connection(hostname, incoming_port, outgoing_port);
    if (!connection) {
        return -1;
    }

    struct tftp_file f;
    f.fd = open(filename, O_RDONLY);
    if (f.fd < 0) {
        fprintf(stderr, "failed to open %s: err=%d\n", filename, errno);
        return -1;
    }
    struct stat st;
    if (fstat(f.fd, &st) < 0) {
        fprintf(stderr, "could not get file size: err=%d\n", errno);
        return -1;
    }
    long file_size = st.st_size;

    fprintf(stdout, "Sending %s of size %ld\n", filename, file_size);


    size_t out = SCRATCHSZ;
    size_t in = SCRATCHSZ;
    void* outgoing = (void*)out_scratch;
    void* incoming = (void*)in_scratch;
    uint32_t timeout_ms = 60000;

    tftp_status s =
        tftp_generate_write_request(session,
                                    "magenta.bin",
                                    MODE_OCTET,
                                    file_size,
                                    BLOCKSZ, // block_size
                                    0,   // timeout
                                    WINSZ,  // window_size
                                    outgoing,
                                    &out,
                                    &timeout_ms);
    if (s < 0) {
        fprintf(stderr, "Failed to generate write request\n");
        return -1;
    }
    if (!out) {
        fprintf(stderr, "no write request generated!\n");
        return -1;
    }

    int n = connection_send(connection, outgoing, out);
    if (n < 0) {
        fprintf(stderr, "could not send data\n");
        return -1;
    }
    fprintf(stdout, "Sent %d\n", n);

    int ret;
    bool block = true;
    int pending = 0;
    do {
        connection_set_timeout(connection, timeout_ms);

        in = SCRATCHSZ;
        n = connection_receive(connection, incoming, in, block);
        if (n < 0) {
            if (pending && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                out = SCRATCHSZ;
                ret = tftp_prepare_data(session,
                                        outgoing,
                                        &out,
                                        &timeout_ms,
                                        &f);
                if (out) {
                    n = connection_send(connection, outgoing, out);
                    if (n < 0) {
                        fprintf(stderr, "could not send data\n");
                        return -1;
                    }
                }
                if (ret < 0) {
                    fprintf(stderr, "failed to prepare data to send\n");
                    return -1;
                }
                if (!tftp_session_has_pending(session)) {
                    pending = 0;
                    block = true;
                }
                continue;
            }
            if (errno == EAGAIN) {
                fprintf(stdout, "Timed out\n");
                ret = tftp_timeout(session,
                                   outgoing,
                                   &out,
                                   &timeout_ms,
                                   connection);
                if (out) {
                    n = connection_send(connection, outgoing, out);
                    if (n < 0) {
                        fprintf(stderr, "could not send data\n");
                        return -1;
                    }
                }
                if (ret < 0) {
                    fprintf(stderr, "Failed to parse request (%d)\n", ret);
                    return -1;
                }
                continue;
            }
            fprintf(stdout, "Failed %d\n", errno);
            return -1;
        }
        fprintf(stdout, "Received %d\n", n);
        in = n;

        out = SCRATCHSZ;
        ret = tftp_handle_msg(session,
                              incoming,
                              in,
                              outgoing,
                              &out,
                              &timeout_ms,
                              &f);
        if (out) {
            n = connection_send(connection, outgoing, out);
            if (n < 0) {
                fprintf(stderr, "could not send data\n");
                return -1;
            }
        }
        if (ret < 0) {
            fprintf(stderr, "Failed to parse request (%d)\n", ret);
            return -1;
        } else if (ret == TFTP_TRANSFER_COMPLETED) {
            fprintf(stderr, "Completed\n");
            return 0;
        }
        if (tftp_session_has_pending(session)) {
            pending = 1;
            block = false;
        } else {
            pending = 0;
            block = true;
        }
    } while (1);

    return 0;
}

tftp_status tftp_receive_file(tftp_session* session,
                              const char* hostname,
                              int incoming_port,
                              int outgoing_port,
                              const char* filename) {
    connection_t* connection = create_connection(hostname, incoming_port, outgoing_port);
    size_t in = SCRATCHSZ;
    void* incoming = (void*)in_scratch;
    size_t out = SCRATCHSZ;
    void* outgoing = (void*)out_scratch;
    uint32_t timeout_ms = 60000;

    if (!connection) {
        return -1;
    }

    struct tftp_file f;
    f.fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (f.fd < 0) {
        fprintf(stderr, "failed to open %s: err=%d\n", filename, errno);
        return -1;
    }

    fprintf(stdout, "Waiting for traffic.\n");

    int n, ret;
    int wrq_received = 0;
    do {
        in = SCRATCHSZ;
        n = connection_receive(connection, incoming, in, true);
        if (n < 0) {
            if (errno == EAGAIN) {
                fprintf(stdout, "Timed out\n");
                if (wrq_received) {
                    out = SCRATCHSZ;
                    ret = tftp_timeout(session,
                                       outgoing,
                                       &out,
                                       &timeout_ms,
                                       &f);
                    if (out) {
                        n = connection_send(connection, outgoing, out);
                        if (n < 0) {
                            fprintf(stderr, "could not send data\n");
                            return -1;
                        }
                    }
                    if (ret < 0) {
                        fprintf(stderr, "Failed to parse request (%d)\n", ret);
                        return -1;
                    }
                }
                continue;
            } else {
                fprintf(stdout, "Failed to receive: -%d\n", errno);
                return -1;
            }
        } else {
            fprintf(stdout, "Received: %d\n", n);
            in = n;
            wrq_received = 1;
        }

        out = SCRATCHSZ;
        ret = tftp_handle_msg(session,
                              incoming,
                              in,
                              outgoing,
                              &out,
                              &timeout_ms,
                              &f);
        if (out) {
            n = connection_send(connection, outgoing, out);
            if (n < 0) {
                fprintf(stderr, "could not send data\n");
                return -1;
            }
        }
        if (ret < 0) {
            fprintf(stderr, "Failed to parse request (%d)\n", ret);
            return -1;
        } else if (ret == TFTP_TRANSFER_COMPLETED) {
            fprintf(stderr, "Completed %zu ... ", f.size);
            close(f.fd);
            fprintf(stderr, "Flushed to disk\n");
            return 0;
        }
        connection_set_timeout(connection, timeout_ms);
    } while (1);
    return 0;
}

int main(int argc, char* argv[]) {
    const char* hostname = "127.0.0.1";
    int port = 2343;

    srand(time(NULL));

    if (argc < 3) {
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

    tftp_session_set_open_cb(session, receive_open_file);
    tftp_session_set_read_cb(session, read_file);
    tftp_session_set_write_cb(session, write_file);

    if (!strncmp(argv[1], "-s", 2)) {
        return tftp_send_file(session, hostname, port, port + 1, argv[2]);
    } else if (!strncmp(argv[1], "-r", 2)) {
        return tftp_receive_file(session, hostname, port + 1, port, argv[2]);
    } else {
        print_usage();
        return 2;
    }
    return 0;
}
