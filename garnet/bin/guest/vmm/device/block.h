// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_DEVICE_BLOCK_H_
#define GARNET_BIN_GUEST_VMM_DEVICE_BLOCK_H_

#include <stdint.h>

// From Virtio 1.0, Section 5.2.4: The capacity of the device (expressed in
// 512-byte sectors) is always present.
static constexpr size_t kBlockSectorSize = 512;

#endif  // GARNET_BIN_GUEST_VMM_DEVICE_BLOCK_H_
