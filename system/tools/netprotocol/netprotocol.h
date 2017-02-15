// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/netboot.h>

#define MAXSIZE 1024

typedef struct {
    struct nbmsg_t hdr;
    uint8_t data[MAXSIZE];
} msg;

struct sockaddr_in6;

int netboot_open(const char* hostname, unsigned port, struct sockaddr_in6* addr_out);

int netboot_txn(int s, msg* in, msg* out, int outlen);
