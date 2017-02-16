// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is an example binary to exercise ulib/tftp. It runs on Linux or MacOs.

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "tftp/tftp.h"

#define BLOCKSZ 100
#define WINSZ   10

static char scratch[1024];
static char out_scratch[1024];
static char in_scratch[1024];
static char* receiving;
static size_t receiving_length;

typedef struct connection connection_t;

struct connection {
    int socket;
    struct sockaddr_in out_addr;
    struct sockaddr_in in_addr;
    uint32_t previous_timeout_ms;
};

int connection_send(connection_t* connection, void* data, size_t len) {
    return sendto(connection->socket, data, len, 0, (struct sockaddr*)&connection->out_addr,
            sizeof(struct sockaddr_in));
}

int connection_receive(connection_t* connection, void* data, size_t len) {
    socklen_t server_len;
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
#if defined(__APPLE__)
    void* server_addr = server->h_addr;
#else
    void* server_addr = server->h_addr_list[0];
#endif
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

tftp_status send_message(void* data, size_t length, void* cookie) {
    connection_t* connection = (connection_t*)cookie;
    int n = connection_send(connection, data, length);
    fprintf(stdout, "Sent %d\n", n);
    return n;
}

tftp_status receive_open_file(const char* filename,
                              size_t size,
                              void** data,
                              void* cookie) {
    fprintf(stdout, "Allocating %ld\n", size);
    receiving = (char*)malloc(size);
    receiving_length = size;
    memset(receiving, 0, size);
    *data = (void*)receiving;
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

    // Open file and retrieve file size
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return -1;
    }
    if (fseek(file, 0, SEEK_END)) {
        fprintf(stderr, "Failed to determine file size\n");
        fclose(file);
        return -1;
    }
    long file_size = ftell(file);
    if (fseek(file, 0, SEEK_SET)) {
        fprintf(stderr, "Failed to determine file size\n");
        fclose(file);
        return -1;
    }

    fprintf(stdout, "Sending %s of size %ld\n", filename, file_size);


    // FIXME we obviously don't want to load everything in memory at once!
    void* data = malloc(file_size);
    memset(data, 0, file_size);
    size_t offset = 0;
    fprintf(stdout, "Loading file into memory...");
    while (!feof(file)) {
        offset += fread(data + offset, 1, 4096, file);
    }
    fprintf(stdout, " done %zu\n", offset);

    size_t out = 1024;
    size_t in = 1024;
    void* outgoing = (void*)out_scratch;
    void* incoming = (void*)in_scratch;
    uint32_t timeout_ms = 60000;

    if (tftp_generate_write_request(session,
                                    "magenta.bin",
                                    MODE_OCTET,
                                    data,
                                    file_size,
                                    BLOCKSZ, // block_size
                                    0,   // timeout
                                    WINSZ,  // window_size
                                    outgoing,
                                    &out,
                                    &timeout_ms,
                                    connection)) {
        fprintf(stderr, "Failed to generate write request\n");
        return -1;
    }

    int n, ret;
    do {
        connection_set_timeout(connection, timeout_ms);

        in = 1024;
        n = connection_receive(connection, incoming, in);
        if (n < 0) {
            if (errno == EAGAIN) {
                fprintf(stdout, "Timed out\n");
                ret = tftp_timeout(session,
                                   outgoing,
                                   &out,
                                   &timeout_ms,
                                   connection);
                if (ret < 0) {
                    fprintf(stderr, "Failed to parse request (%d)\n", ret);
                    return -1;
                } else if (ret > 0) {
                    fprintf(stderr, "Completed\n");
                    return 0;
                }
                continue;
            } else {
                fprintf(stdout, "Failed %d\n", errno);
                return -1;
            }
        }
        fprintf(stdout, "Received %d\n", n);
        in = n;

        out = 1024;
        ret = tftp_handle_msg(session,
                              incoming,
                              in,
                              outgoing,
                              &out,
                              &timeout_ms,
                              connection);
        if (ret < 0) {
            fprintf(stderr, "Failed to parse request (%d)\n", ret);
            return -1;
        } else if (ret > 0) {
            fprintf(stderr, "Completed\n");
            return 0;
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
    size_t in = 1024;
    void* incoming = (void*)in_scratch;
    size_t out = 1024;
    void* outgoing = (void*)out_scratch;
    uint32_t timeout_ms = 60000;

    if (!connection) {
        return -1;
    }

    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return -1;
    }

    fprintf(stdout, "Waiting for traffic.\n");

    int n, ret;
    do {
        in = 1024;
        n = connection_receive(connection, incoming, in);
        if (n < 0) {
            if (errno == EAGAIN) {
                fprintf(stdout, "Timed out\n");
            } else {
                fprintf(stdout, "Failed to receive: -%d\n", errno);
                return -1;
            }
        } else {
            fprintf(stdout, "Received: %d\n", n);
            in = n;
        }

        out = 1024;
        ret = tftp_handle_msg(session,
                              incoming,
                              in,
                              outgoing,
                              &out,
                              &timeout_ms,
                              connection);
        if (ret < 0) {
            fprintf(stderr, "Failed to parse request (%d)\n", ret);
            return -1;
        } else if (ret > 0) {
            fprintf(stderr, "Completed %zu ... ", receiving_length);
            FILE* file = fopen(filename, "w");
            out = 0;
            while (out < receiving_length) {
                size_t length = receiving_length - out < 4096 ? receiving_length - out : 4096;
                ret = fwrite(receiving + out, length, 1, file);
                if (ret <= 0) {
                    fprintf(stderr, "\nFailed to write to disk %d\n", ret);
                    fclose(file);
                    return -1;
                }
                out += length;
            }
            fclose(file);
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

    if (argc < 3) {
        print_usage();
        return 1;
    }

    tftp_session* session = NULL;
    if (tftp_init(&session, scratch, 1024)) {
        fprintf(stderr, "Failed to initialize TFTP Session\n");
        return -1;
    }
    tftp_session_set_send_cb(session, send_message);
    tftp_session_set_open_cb(session, receive_open_file);

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
