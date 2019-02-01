// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/protocol/mac.h>

namespace wlan {

class WlanmacIfcClient {
   public:
    WlanmacIfcClient() : ifc_(nullptr), cookie_(nullptr) {}

    WlanmacIfcClient(wlanmac_ifc_t* ifc, void* cookie) : ifc_(ifc), cookie_(cookie) {}

    operator bool() const { return ifc_; }

    void Status(uint32_t status) { ifc_->status(cookie_, status); }

    void Recv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info) {
        ifc_->recv(cookie_, flags, data, length, info);
    }

    void ReportTxStatus(const wlan_tx_status_t* tx_status) {
        ifc_->report_tx_status(cookie_, tx_status);
    }

    void CompleteTx(wlan_tx_packet_t* packet, zx_status_t status) {
        ifc_->complete_tx(cookie_, packet, status);
    }

   private:
    wlanmac_ifc_t* ifc_;
    void* cookie_;
};

}  // namespace wlan
