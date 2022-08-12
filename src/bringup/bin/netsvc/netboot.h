// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_NETBOOT_H_
#define SRC_BRINGUP_BIN_NETSVC_NETBOOT_H_

#include <zircon/boot/netboot.h>

#include "src/bringup/bin/netsvc/inet6.h"

void netboot_advertise(const char* nodename);

void netboot_recv(void* data, size_t len, bool is_mcast, const ip6_addr_t* daddr, uint16_t dport,
                  const ip6_addr_t* saddr, uint16_t sport);

extern "C" void netboot_run_cmd(const char* cmd);

// Ask for a buffer suitable to put the file `name` in
// Return NULL to indicate `name` is not wanted.
nbfile* netboot_get_buffer(const char* name, size_t size);

#endif  // SRC_BRINGUP_BIN_NETSVC_NETBOOT_H_
