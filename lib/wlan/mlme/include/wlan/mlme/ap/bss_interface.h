// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/wlan_mlme.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>

#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include <chrono>

namespace wlan {

class Buffer;
class StartRequest;

using aid_t = size_t;
static constexpr aid_t kGroupAdressedAid = 0;
static constexpr aid_t kMaxBssClients = 2008;

class BssInterface {
   public:
    virtual const common::MacAddr& bssid() const = 0;
    virtual uint64_t timestamp() = 0;

    // Starts the BSS. Beacons will be sent and incoming frames are processed.
    virtual void Start(const wlan_mlme::StartRequest& req) = 0;
    // Stops the BSS. All incoming frames are dropped and Beacons are not sent anymore.
    virtual void Stop() = 0;

    // Assigns an AID to the given client. Returns an error if there is no AID available anymore.
    virtual zx_status_t AssignAid(const common::MacAddr& client, aid_t* out_aid) = 0;
    // Releases the AID associated with the given client. The AID will be available afterwards and
    // can get assigned to other, newly associated clients.
    virtual zx_status_t ReleaseAid(const common::MacAddr& client) = 0;

    virtual seq_t NextSeq(const MgmtFrameHeader& hdr) = 0;
    virtual seq_t NextSeq(const MgmtFrameHeader& hdr, uint8_t aci) = 0;
    virtual seq_t NextSeq(const DataFrameHeader& hdr) = 0;

    virtual zx_status_t EthToDataFrame(const ImmutableBaseFrame<EthernetII>& frame,
                                       fbl::unique_ptr<Packet>* out_packet) = 0;

    virtual bool IsHTReady() const = 0;
    virtual bool IsCbw40RxReady() const = 0;
    virtual bool IsCbw40TxReady() const = 0;
    virtual HtCapabilities BuildHtCapabilities() const = 0;
    virtual HtOperation BuildHtOperation(const wlan_channel_t& chan) const = 0;

    virtual zx_status_t SendMgmtFrame(fbl::unique_ptr<Packet> packet) = 0;
    virtual zx_status_t SendDataFrame(fbl::unique_ptr<Packet> packet) = 0;
    virtual zx_status_t SendEthFrame(fbl::unique_ptr<Packet> packet) = 0;

    virtual wlan_channel_t Chan() const = 0;
};

}  // namespace wlan
