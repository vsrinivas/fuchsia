// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_CONFIG_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_CONFIG_H_

#include <zircon/types.h>

static constexpr zx_signals_t kDeviceInterruptShift = __builtin_ctz(ZX_USER_SIGNAL_ALL);

// Virtio 1.0 Section 4.1.4.4: notify_off_multiplier is combined with the
// queue_notify_off to derive the Queue Notify address within a BAR for a
// virtqueue:
//
//      cap.offset + queue_notify_off * notify_off_multiplier
//
// Virtio 1.0 Section 4.1.4.4.1: The device MUST either present
// notify_off_multiplier as an even power of 2, or present
// notify_off_multiplier as 0.
//
// By using a multiplier of 4, we use sequential 4b words to notify, ex:
//
//      cap.offset + 0  -> Notify Queue 0
//      cap.offset + 4  -> Notify Queue 1
//      ...
//      cap.offset + 4n -> Notify Queue n
static constexpr size_t kQueueNotifyMultiplier = 4;

constexpr uint16_t queue_from(zx_gpaddr_t base, zx_gpaddr_t off) {
  return static_cast<uint16_t>((off - base) / kQueueNotifyMultiplier);
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_CONFIG_H_
