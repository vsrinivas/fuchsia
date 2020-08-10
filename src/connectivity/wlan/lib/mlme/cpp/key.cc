// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <optional>

#include <wlan/mlme/key.h>
#include <wlan/protocol/mac.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

std::optional<wlan_key_config_t> ToKeyConfig(const wlan_mlme::SetKeyDescriptor& key_descriptor) {
  uint8_t key_type;
  switch (key_descriptor.key_type) {
    case wlan_mlme::KeyType::PAIRWISE:
      key_type = WLAN_KEY_TYPE_PAIRWISE;
      break;
    case wlan_mlme::KeyType::PEER_KEY:
      key_type = WLAN_KEY_TYPE_PEER;
      break;
    case wlan_mlme::KeyType::IGTK:
      key_type = WLAN_KEY_TYPE_IGTK;
      break;
    default:
      key_type = WLAN_KEY_TYPE_GROUP;
      break;
  }

  wlan_key_config_t key_config = {};
  memcpy(key_config.key, key_descriptor.key.data(), key_descriptor.key.size());
  key_config.key_type = key_type;
  key_config.key_len = static_cast<uint8_t>(key_descriptor.key.size());
  key_config.key_idx = key_descriptor.key_id;
  key_config.protection = WLAN_PROTECTION_RX_TX;
  key_config.cipher_type = key_descriptor.cipher_suite_type;
  key_config.rsc = key_descriptor.rsc;
  memcpy(key_config.cipher_oui, key_descriptor.cipher_suite_oui.data(),
         sizeof(key_config.cipher_oui));
  memcpy(key_config.peer_addr, key_descriptor.address.data(), sizeof(key_config.peer_addr));
  return key_config;
}

}  // namespace wlan
