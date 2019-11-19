// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_MAC_H_
#define GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_MAC_H_

#include <ddk/protocol/wlan/info.h>
#include <ddk/protocol/wlan/mac.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// LINT.IfChange
typedef int8_t wlan_dBm_t;
typedef int16_t wlan_dBmh_t;
typedef int8_t wlan_dB_t;
typedef int16_t wlan_dBh_t;
// LINT.ThenChange(//src/connectivity/wlan/lib/common/cpp/include/wlan/common/energy.h)

// TxVector is defined in //src/connectivity/wlan/lib/common/cpp/tx_vector.h
typedef uint16_t tx_vec_idx_t;

__END_CDECLS

#endif  // GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_MAC_H_
