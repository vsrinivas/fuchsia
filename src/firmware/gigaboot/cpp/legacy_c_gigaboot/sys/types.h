// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_LEGACY_C_GIGABOOT_SYS_TYPES_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_LEGACY_C_GIGABOOT_SYS_TYPES_H_

#include_next <sys/types.h>

// Use typedef instead of using since the header is used in c source.
typedef size_t off_t;

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_LEGACY_C_GIGABOOT_SYS_TYPES_H_
