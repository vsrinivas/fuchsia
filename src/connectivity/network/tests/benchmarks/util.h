// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTS_BENCHMARKS_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_TESTS_BENCHMARKS_UTIL_H_

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/uio.h>

#include <memory>

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

#include "src/lib/fxl/logging.h"

void TraceSend(int sendfd, uint8_t* buf, ssize_t size, const char* name, struct sockaddr_in* addr,
               socklen_t addrlen);
void TraceRecv(int recvfd, uint8_t* buf, ssize_t size, const char* name, struct sockaddr_in* addr,
               socklen_t addrlen);

#endif  // SRC_CONNECTIVITY_NETWORK_TESTS_BENCHMARKS_UTIL_H_
