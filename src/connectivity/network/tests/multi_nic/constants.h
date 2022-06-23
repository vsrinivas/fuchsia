// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTS_MULTI_NIC_CONSTANTS_H_
#define SRC_CONNECTIVITY_NETWORK_TESTS_MULTI_NIC_CONSTANTS_H_

#include <stdint.h>

constexpr char kClientNic1Name[] = "client-ep-1";
constexpr char kClientNic2Name[] = "client-ep-2";

constexpr char kClientIpv4Addr1[] = "192.168.0.1";
constexpr char kClientIpv4Addr2[] = "192.168.0.2";
constexpr char kServerIpv4Addr[] = "192.168.0.254";

constexpr char kClientIpv6Addr1[] = "a::1";
constexpr char kClientIpv6Addr2[] = "a::2";
constexpr char kServerIpv6Addr[] = "a::ffff";

constexpr uint16_t kServerPort = 1234;

#endif  // SRC_CONNECTIVITY_NETWORK_TESTS_MULTI_NIC_CONSTANTS_H_
