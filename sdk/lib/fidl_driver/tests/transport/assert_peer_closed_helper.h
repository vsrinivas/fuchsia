// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_TESTS_TRANSPORT_ASSERT_PEER_CLOSED_HELPER_H_
#define LIB_FIDL_DRIVER_TESTS_TRANSPORT_ASSERT_PEER_CLOSED_HELPER_H_

#include <lib/fdf/cpp/channel.h>
#include <lib/fidl_driver/cpp/transport.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

namespace fidl_driver_testing {

// Generates a test failure if the peer of |channel| is not closed.
void AssertPeerClosed(const zx::channel& channel);

// Generates a test failure if the peer of |channel| is not closed.
void AssertPeerClosed(const fdf::Channel& channel);

}  // namespace fidl_driver_testing

#endif  // LIB_FIDL_DRIVER_TESTS_TRANSPORT_ASSERT_PEER_CLOSED_HELPER_H_
