// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/rust_utils.h>

namespace wlan {

SequenceManager NewSequenceManager() {
  return SequenceManager(mlme_sequence_manager_new(), mlme_sequence_manager_delete);
}

ClientStation NewClientStation(mlme_device_ops_t device, mlme_buffer_provider_ops_t buf_provider,
                               common::MacAddr bssid, common::MacAddr iface_mac) {
  return ClientStation(client_sta_new(device, buf_provider, &bssid.byte, &iface_mac.byte),
                       client_sta_delete);
}

}  // namespace wlan
