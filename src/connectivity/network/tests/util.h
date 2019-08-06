// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_

#include <netinet/in.h>
#include <sys/socket.h>

#include "gtest/gtest.h"

// TODO(iyerm): 10s seems too long for our test-cases
const int32_t kTimeout = 10000;  // 10 seconds

void NotifySuccess(int ntfyfd);
void NotifyFail(int ntfyfd);
bool WaitSuccess(int ntfyfd, int timeout);
void StreamAcceptRead(int acptfd, std::string* out, int ntfyfd);
void StreamConnectRead(struct sockaddr_in* addr, std::string* out, int ntfyfd);
void StreamAcceptWrite(int acptfd, const char* msg, int ntfyfd);
void DatagramRead(int recvfd, std::string* out, struct sockaddr_in* addr, socklen_t* addrlen,
                  int ntfyfd, int timeout);
void DatagramReadWrite(int recvfd, int ntfyfd);
void DatagramReadWriteV6(int recvfd, int ntfyfd);
#endif  // SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
