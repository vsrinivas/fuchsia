// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/protocol/mac.h>

namespace wlan {

void FillEtherLlcHeader(LlcHeader* llc, uint16_t protocol_id_be) {
  llc->dsap = kLlcSnapExtension;
  llc->ssap = kLlcSnapExtension;
  llc->control = kLlcUnnumberedInformation;
  std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
  llc->protocol_id_be = protocol_id_be;
}

}  // namespace wlan
