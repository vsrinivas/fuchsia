// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <mxio/util.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const int16_t port = 8011;

static const char* sa_to_str(const struct sockaddr* sa, char* str, size_t strlen) {
    if (sa->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)sa;
        return inet_ntop(AF_INET, &sin->sin_addr, str, strlen);
    } else if (sa->sa_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)sa;
        return inet_ntop(AF_INET6, &sin6->sin6_addr, str, strlen);
    }
    return NULL;
}

static int client() {
    struct addrinfo hints;
    struct addrinfo *result = NULL;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int r = getaddrinfo("127.0.0.1", "8011", &hints, &result);
    if (r != 0) {
        printf("client: getaddrinfo failed (%d, errno = %d)\n", r, errno);
        return -1;
    }

    int s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s < 0) {
        printf("client: socket failed: %d\n", errno);
	return -1;
    }
    if (connect(s, result->ai_addr, result->ai_addrlen) < 0) {
        printf("client: connect failed: %d\n", errno);
	return -1;
    }
    char str[INET6_ADDRSTRLEN];
    printf("client: connected to %s\n", sa_to_str(result->ai_addr, str, sizeof(str)));

    close(s);

    return 0;
}

static int server() {
    int s = socket(AF_INET6, SOCK_STREAM, 0);
    if (s < 0) {
        printf("server: socket failed: %d\n", errno);
        return -1;
    }

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("server: bind failed: %d\n", errno);
        return -1;
    }
    if (listen(s, 1) < 0) {
        printf("server: listen failed: %d\n", errno);
        return -1;
    }


    socklen_t addrlen = sizeof(addr);
    int conn = accept(s, (struct sockaddr*)&addr, &addrlen);
    if (conn < 0) {
        printf("server: accept failed: %d\n", errno);
    }
    char buf[5];
    read(conn, buf, countof(buf)); // wait until client closes
    close(conn);
    close(conn); // double close should not crash the network stack
    return -1;
}

int main(int argc, char** argv) {
    if (argc == 2 && argv[1][0] == 'c') {
        printf("closetest: client\n");
        return client();
    }
    printf("closetest: server\n");

    int ret = server();
    return ret;
}
