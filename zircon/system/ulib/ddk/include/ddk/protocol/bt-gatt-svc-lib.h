// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string.h>

#include <ddk/protocol/bt/gattsvc.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// BT SIG Base UUID for all 16/32 assigned UUID values.
//
//    "00000000-0000-1000-8000-00805F9B34FB"
//
// (see Core Spec v5.0, Vol 3, Part B, Section 2.5.1)
#define BT_GATT_BASE_UUID                                                       \
    {                                                                           \
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, \
            0x00, 0x00, 0x00, 0x00                                              \
    }

#define __BT_UUID_ASSIGNED_OFFSET 12

// Convenience function to make a UUID from a 32-bit assigned value.
static inline bt_gatt_uuid_t bt_gatt_make_uuid32(uint32_t value) {
    bt_gatt_uuid_t retval = {.bytes = BT_GATT_BASE_UUID};

    retval.bytes[__BT_UUID_ASSIGNED_OFFSET] = (uint8_t)(value);
    retval.bytes[__BT_UUID_ASSIGNED_OFFSET + 1] = (uint8_t)(value >> 8);
    retval.bytes[__BT_UUID_ASSIGNED_OFFSET + 2] = (uint8_t)(value >> 16);
    retval.bytes[__BT_UUID_ASSIGNED_OFFSET + 3] = (uint8_t)(value >> 24);

    return retval;
}

// Convenience function to make a UUID from a 16-bit assigned value.
static inline bt_gatt_uuid_t bt_gatt_make_uuid16(uint16_t value) {
    return bt_gatt_make_uuid32((uint32_t)value);
}
// UUID comparison.
// Note: this method only does a binary comparison and doesn't break out low,
// mid, high, version, sequence, or node parts for individual comparison so
// doesn't conform to standard UUID sort.
static inline int bt_gatt_compare_uuid(const bt_gatt_uuid_t* u1,
                                       const bt_gatt_uuid_t* u2) {
    return memcmp(u1->bytes, u2->bytes, 16);
}

__END_CDECLS
