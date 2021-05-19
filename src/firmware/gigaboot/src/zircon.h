// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_ZIRCON_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_ZIRCON_H_

#include <stdint.h>

// Stage a file which will be added as a ZBI item on boot.
int zircon_stage_zbi_file(const char* name, const uint8_t* data, size_t data_len);

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_ZIRCON_H_
