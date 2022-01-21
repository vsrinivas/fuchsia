// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_

#include <sys/types.h>

#include <chrono>

constexpr std::chrono::duration kTimeout = std::chrono::seconds(10);

void fill_stream_send_buf(int fd, int peer_fd, ssize_t *out_bytes_written);

#endif  // SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
