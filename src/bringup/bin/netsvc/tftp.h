// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_TFTP_H_
#define SRC_BRINGUP_BIN_NETSVC_TFTP_H_

#include <lib/async/dispatcher.h>

#include "src/bringup/bin/netsvc/inet6.h"

void tftp_recv(async_dispatcher_t* dispatcher, void* data, size_t len, const ip6_addr_t* daddr,
               uint16_t dport, const ip6_addr_t* saddr, uint16_t sport);

#endif  // SRC_BRINGUP_BIN_NETSVC_TFTP_H_
