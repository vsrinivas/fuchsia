// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/mlme/ap/beacon_sender.h>
#include <wlan/mlme/ap/bss_client_map.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/remote_client.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/sequence.h>

#include <wlan/common/macaddr.h>
#include <zircon/types.h>

namespace wlan {

class ObjectId;
template <typename T> class MlmeMsg;

// An infrastructure BSS which keeps track of its client and owned by the AP
// MLME.
class InfraBss : public BssInterface, public RemoteClient::Listener {
   public:
    InfraBss(DeviceInterface* device, fbl::unique_ptr<BeaconSender> bcn_sender,
             const common::MacAddr& bssid);
    virtual ~InfraBss();

    // Starts the BSS. Beacons will be sent and incoming frames are processed.
    void Start(const MlmeMsg<::fuchsia::wlan::mlme::StartRequest>&);
    // Stops the BSS. All incoming frames are dropped and Beacons are not sent
    // anymore.
    void Stop();
    bool IsStarted();

    // Entry point for ethernet and WLAN frames.
    void HandleAnyFrame(fbl::unique_ptr<Packet>);
    zx_status_t HandleTimeout(const common::MacAddr& client_addr);

    // BssInterface implementation
    const common::MacAddr& bssid() const override;
    uint64_t timestamp() override;
    zx_status_t AssignAid(const common::MacAddr& client, aid_t* out_aid) override;
    zx_status_t ReleaseAid(const common::MacAddr& client) override;

    zx_status_t SendMgmtFrame(fbl::unique_ptr<Packet> packet) override;
    zx_status_t SendDataFrame(fbl::unique_ptr<Packet> packet) override;
    zx_status_t SendEthFrame(fbl::unique_ptr<Packet> packet) override;

    seq_t NextSeq(const MgmtFrameHeader& hdr) override;
    seq_t NextSeq(const MgmtFrameHeader& hdr, uint8_t aci) override;
    seq_t NextSeq(const DataFrameHeader& hdr) override;

    zx_status_t EthToDataFrame(const EthFrame& frame, fbl::unique_ptr<Packet>* out_packet) override;
    void OnPreTbtt() override;
    void OnBcnTxComplete() override;

    bool IsRsn() const override;
    bool IsHTReady() const override;
    bool IsCbw40RxReady() const override;
    bool IsCbw40TxReady() const override;
    HtCapabilities BuildHtCapabilities() const override;
    HtOperation BuildHtOperation(const wlan_channel_t& chan) const override;

    wlan_channel_t Chan() const override { return chan_; }

   private:
    void HandleEthFrame(EthFrame&&);
    void HandleAnyWlanFrame(fbl::unique_ptr<Packet>);
    void HandleAnyMgmtFrame(MgmtFrame<>&&);
    void HandleAnyDataFrame(DataFrame<>&&);
    void HandleAnyCtrlFrame(CtrlFrame<>&&);
    void HandleNewClientAuthAttempt(const MgmtFrameView<Authentication>&);

    // Maximum number of group addressed packets buffered while at least one
    // client is dozing.
    // TODO(NET-687): Find good BU limit.
    static constexpr size_t kMaxGroupAddressedBu = 128;

    // RemoteClient::Listener implementation
    zx_status_t HandleClientDeauth(const common::MacAddr& client) override;
    void HandleClientBuChange(const common::MacAddr& client, size_t bu_count) override;

    zx_status_t CreateClientTimer(const common::MacAddr& client_addr,
                                  fbl::unique_ptr<Timer>* out_timer);
    // Returns `true` if a frame with the given destination should get buffered.
    bool ShouldBufferFrame(const common::MacAddr& dest) const;
    zx_status_t BufferFrame(fbl::unique_ptr<Packet> packet);
    zx_status_t SendNextBu();

    const common::MacAddr bssid_;
    DeviceInterface* device_;
    fbl::unique_ptr<BeaconSender> bcn_sender_;
    zx_time_t started_at_;
    BssClientMap clients_;
    Sequence seq_;
    // Queue which holds buffered non-GCR-SP frames when at least one client is
    // dozing.
    PacketQueue bu_queue_;
    PsCfg ps_cfg_;
    wlan_channel_t chan_;
    // MLME-START.request holds all information required to correctly configure
    // and start a BSS.
    ::fuchsia::wlan::mlme::StartRequest start_req_;
};

}  // namespace wlan
