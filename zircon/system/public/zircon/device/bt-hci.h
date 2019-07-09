// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_BT_HCI_H_
#define SYSROOT_ZIRCON_DEVICE_BT_HCI_H_

// clang-format off

#include <stdint.h>

__BEGIN_CDECLS

// Potential values for the flags bitfield in a snoop channel packet.
typedef uint32_t bt_hci_snoop_type_t;
#define BT_HCI_SNOOP_TYPE_CMD ((bt_hci_snoop_type_t)0)
#define BT_HCI_SNOOP_TYPE_EVT ((bt_hci_snoop_type_t)1)
#define BT_HCI_SNOOP_TYPE_ACL ((bt_hci_snoop_type_t)2)
#define BT_HCI_SNOOP_TYPE_SCO ((bt_hci_snoop_type_t)3)

#define BT_HCI_SNOOP_FLAG_RECV 0x04 // Host -> Controller

static inline uint8_t bt_hci_snoop_flags(bt_hci_snoop_type_t type, bool is_received) {
  return (uint8_t)(type | (is_received ? BT_HCI_SNOOP_FLAG_RECV : 0x00));
}

__END_CDECLS

#endif  // SYSROOT_ZIRCON_DEVICE_BT_HCI_H_
