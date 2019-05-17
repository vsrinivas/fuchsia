// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inet6/inet6.h>

#define TFTP_TIMEOUT_SECS 1

zx_time_t tftp_next_timeout();

void tftp_recv(void* data, size_t len, const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport);

void tftp_timeout_expired();

bool tftp_has_pending();
void tftp_send_next();
