// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_EAPOL_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_EAPOL_H_

namespace wlan {
namespace eapol {

enum class PortState : bool { kBlocked = false, kOpen = true };

}  // namespace eapol
}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_EAPOL_H_
