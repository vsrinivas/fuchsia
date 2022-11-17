// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PROC_LIB_LINUX_UAPI_STUB_BITS_IN_ADDR_H_
#define SRC_PROC_LIB_LINUX_UAPI_STUB_BITS_IN_ADDR_H_

#include <stdint.h>

/** An integral type representing an IPv4 address. */
typedef uint32_t in_addr_t;

/** A structure representing an IPv4 address. */
struct in_addr {
  in_addr_t s_addr;
};

#endif  // SRC_PROC_LIB_LINUX_UAPI_STUB_BITS_IN_ADDR_H_
