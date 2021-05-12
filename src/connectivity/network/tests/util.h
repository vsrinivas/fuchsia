// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_

#include <sys/types.h>

#include <cstdint>

// TODO(iyerm): 10s seems too long for our test-cases
constexpr int kTimeout = 10000;  // 10 seconds

ssize_t fill_stream_send_buf(int fd, int peer_fd);

#endif  // SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
