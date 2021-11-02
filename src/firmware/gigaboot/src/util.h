// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Small utility functions.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_UTIL_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_UTIL_H_

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

uint64_t htonll(uint64_t val);
uint64_t ntohll(uint64_t val);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_UTIL_H_
