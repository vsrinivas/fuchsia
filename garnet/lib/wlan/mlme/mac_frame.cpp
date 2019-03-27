// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mac_frame.h>

#include <wlan/mlme/debug.h>
#include <wlan/mlme/packet.h>

#include <fbl/algorithm.h>
#include <wlan/protocol/mac.h>

namespace wlan {

void FillEtherLlcHeader(LlcHeader* llc, uint16_t protocol_id) {
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = protocol_id;
}

}  // namespace wlan
