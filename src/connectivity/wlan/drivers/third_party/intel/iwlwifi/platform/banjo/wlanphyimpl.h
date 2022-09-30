// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains copies of banjo definitions that were auto generated from
// fuchsia.hardware.wlanphyimpl. Since banjo is being deprecated, we are making a local copy of
// defines that the driver relies upon. fxbug.dev/104598 is the tracking bug to remove the usage
// of platforms/banjo/*.h files.

// WARNING: DO NOT ADD MORE DEFINITIONS TO THIS FILE

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_WLANPHYIMPL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_WLANPHYIMPL_H_

#include <zircon/types.h>

#include "common.h"

typedef struct wlanphy_impl_create_iface_req wlanphy_impl_create_iface_req_t;
typedef struct wlanphy_country wlanphy_country_t;
#define WLANPHY_ALPHA2_LEN UINT8_C(2)
// Parameters to create an interface.
struct wlanphy_impl_create_iface_req {
  // The station role for this interface. A device may support multiple roles,
  // but an interface is instantiated with a single role.
  wlan_mac_role_t role;
  // A handle to the direct MLME channel, if supported by the driver.
  zx_handle_t mlme_channel;
  // Whether this iface creation request come with an initial station address.
  bool has_init_sta_addr;
  // The initial station address set from configuration layer.
  uint8_t init_sta_addr[6];
};

struct wlanphy_country {
  // ISO Alpha-2 takes two octet alphabet characters.
  // This needs to be expanded if at least one WLAN device driver or firmware
  // requires more than two octets.
  uint8_t alpha2[2];
};
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_WLANPHYIMPL_H_
