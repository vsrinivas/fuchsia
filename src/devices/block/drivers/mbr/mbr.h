// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_MBR_MBR_H_
#define SRC_DEVICES_BLOCK_DRIVERS_MBR_MBR_H_

#include <zircon/status.h>

#include <mbr/mbr.h>

namespace mbr {

zx_status_t Parse(const uint8_t* buffer, size_t bufsz, Mbr* out);

}  // namespace mbr

#endif  // SRC_DEVICES_BLOCK_DRIVERS_MBR_MBR_H_
