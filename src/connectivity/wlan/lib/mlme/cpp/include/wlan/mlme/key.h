// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_KEY_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_KEY_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/protocol/mac.h>

#include <optional>

namespace wlan {

std::optional<wlan_key_config_t> ToKeyConfig(
    const ::fuchsia::wlan::mlme::SetKeyDescriptor& key_descriptor);

}  // namespace wlan
#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_KEY_H_
