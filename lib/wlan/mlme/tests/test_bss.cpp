// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_bss.h"
#include "mock_device.h"

#include <wlan/common/channel.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <gtest/gtest.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

zx_status_t WriteSsid(ElementWriter* w, const uint8_t* ssid, size_t ssid_len) {
    if (!w->write<SsidElement>(ssid, ssid_len)) {
        errorf("could not write ssid \"%s\" to Beacon\n",
               debug::ToAsciiOrHexStr(ssid, ssid_len).c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t WriteSupportedRates(ElementWriter* w, const std::vector<SupportedRate>& rates) {
    if (!w->write<SupportedRatesElement>(rates)) {
        errorf("could not write supported rates\n");
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t WriteDsssParamSet(ElementWriter* w, const wlan_channel_t chan) {
    if (!w->write<DsssParamSetElement>(chan.primary)) {
        errorf("could not write DSSS parameters\n");
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t WriteTim(ElementWriter* w, const PsCfg& ps_cfg) {
    size_t bitmap_len = ps_cfg.GetTim()->BitmapLen();
    uint8_t bitmap_offset = ps_cfg.GetTim()->BitmapOffset();

    uint8_t dtim_count = ps_cfg.dtim_count();
    uint8_t dtim_period = ps_cfg.dtim_period();
    ZX_DEBUG_ASSERT(dtim_count != dtim_period);
    if (dtim_count == dtim_period) { warnf("illegal DTIM state"); }

    BitmapControl bmp_ctrl;
    bmp_ctrl.set_offset(bitmap_offset);
    if (ps_cfg.IsDtim()) { bmp_ctrl.set_group_traffic_ind(ps_cfg.GetTim()->HasGroupTraffic()); }
    if (!w->write<TimElement>(dtim_count, dtim_period, bmp_ctrl, ps_cfg.GetTim()->BitmapData(),
                              bitmap_len)) {
        errorf("could not write TIM element\n");
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t WriteCountry(ElementWriter* w, const wlan_channel_t chan) {
    const uint8_t kCountry[3] = {'U', 'S', ' '};

    std::vector<SubbandTriplet> subbands;

    // TODO(porce): Read from the AP's regulatory domain
    if (wlan::common::Is2Ghz(chan)) {
        subbands.push_back({1, 11, 36});
    } else {
        subbands.push_back({36, 4, 36});
        subbands.push_back({52, 4, 30});
        subbands.push_back({100, 12, 30});
        subbands.push_back({149, 5, 36});
    }

    if (!w->write<CountryElement>(kCountry, subbands)) {
        errorf("could not write CountryElement\n");
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t WriteExtendedSupportedRates(ElementWriter* w,
                                        const std::vector<SupportedRate>& ext_rates) {
    if (!w->write<ExtendedSupportedRatesElement>(ext_rates)) {
        errorf("could not write extended supported rates\n");
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t WriteHtCapabilities(ElementWriter* w, const HtCapabilities& htc) {
    if (!w->write<HtCapabilities>(htc.ht_cap_info, htc.ampdu_params, htc.mcs_set, htc.ht_ext_cap,
                                  htc.txbf_cap, htc.asel_cap)) {
        errorf("could not write HtCapabilities\n");
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t WriteHtOperation(ElementWriter* w, const HtOperation& hto) {
    if (!w->write<HtOperation>(hto.primary_chan, hto.head, hto.tail, hto.basic_mcs_set)) {
        errorf("could not write HtOperation\n");
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

wlan_mlme::BSSDescription CreateBssDescription() {
    common::MacAddr bssid(kBssid1);

    wlan_mlme::BSSDescription bss_desc;
    std::memcpy(bss_desc.bssid.mutable_data(), bssid.byte, common::kMacAddrLen);
    std::vector<uint8_t> ssid(kSsid, kSsid + sizeof(kSsid));
    bss_desc.ssid.reset(std::move(ssid));
    bss_desc.bss_type = wlan_mlme::BSSTypes::INFRASTRUCTURE;
    bss_desc.beacon_period = kBeaconPeriodTu;
    bss_desc.dtim_period = kDtimPeriodTu;
    bss_desc.timestamp = 0;
    bss_desc.local_time = 0;

    wlan_mlme::CapabilityInfo cap;
    cap.ess = true;
    cap.short_preamble = true;
    bss_desc.cap = cap;

    bss_desc.rsn.reset();
    bss_desc.rcpi_dbmh = 0;
    bss_desc.rsni_dbh = 0;

    bss_desc.ht_cap.reset();
    bss_desc.ht_op.reset();

    bss_desc.vht_cap.reset();
    bss_desc.vht_op.reset();

    bss_desc.chan.cbw = static_cast<wlan_mlme::CBW>(kBssChannel.cbw);
    bss_desc.chan.primary = kBssChannel.primary;

    bss_desc.rssi_dbm = -35;

    return fbl::move(bss_desc);
}

zx_status_t CreateStartRequest(MlmeMsg<wlan_mlme::StartRequest>* out_msg, bool protected_ap) {
    auto req = wlan_mlme::StartRequest::New();
    std::vector<uint8_t> ssid(kSsid, kSsid + sizeof(kSsid));
    req->ssid.reset(std::move(ssid));
    req->bss_type = wlan_mlme::BSSTypes::INFRASTRUCTURE;
    req->beacon_period = kBeaconPeriodTu;
    req->dtim_period = kDtimPeriodTu;
    req->channel = kBssChannel.primary;
    if (protected_ap) { req->rsne.reset(std::vector<uint8_t>(kRsne, kRsne + sizeof(kRsne))); }

    return WriteServiceMessage(req.get(), fuchsia_wlan_mlme_MLMEStartReqOrdinal, out_msg);
}

zx_status_t CreateJoinRequest(MlmeMsg<wlan_mlme::JoinRequest>* out_msg) {
    common::MacAddr bssid(kBssid1);

    auto req = wlan_mlme::JoinRequest::New();
    req->join_failure_timeout = kJoinTimeout;
    req->nav_sync_delay = 20;
    req->op_rate_set.reset({10, 22, 34});
    req->selected_bss = CreateBssDescription();

    return WriteServiceMessage(req.get(), fuchsia_wlan_mlme_MLMEJoinReqOrdinal, out_msg);
}

zx_status_t CreateAuthRequest(MlmeMsg<wlan_mlme::AuthenticateRequest>* out_msg) {
    common::MacAddr bssid(kBssid1);

    auto req = wlan_mlme::AuthenticateRequest::New();
    std::memcpy(req->peer_sta_address.mutable_data(), bssid.byte, common::kMacAddrLen);
    req->auth_failure_timeout = kAuthTimeout;
    req->auth_type = wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;

    return WriteServiceMessage(req.get(), fuchsia_wlan_mlme_MLMEAuthenticateReqOrdinal, out_msg);
}

zx_status_t CreateAuthResponse(MlmeMsg<wlan_mlme::AuthenticateResponse>* out_msg,
                               wlan_mlme::AuthenticateResultCodes result_code) {
    common::MacAddr client(kClientAddress);

    auto resp = wlan_mlme::AuthenticateResponse::New();
    std::memcpy(resp->peer_sta_address.mutable_data(), client.byte, common::kMacAddrLen);
    resp->result_code = result_code;

    return WriteServiceMessage(resp.get(), fuchsia_wlan_mlme_MLMEAuthenticateRespOrdinal, out_msg);
}

zx_status_t CreateAssocRequest(MlmeMsg<wlan_mlme::AssociateRequest>* out_msg) {
    common::MacAddr bssid(kBssid1);

    auto req = wlan_mlme::AssociateRequest::New();
    std::memcpy(req->peer_sta_address.mutable_data(), bssid.byte, common::kMacAddrLen);
    req->rsn.reset();

    return WriteServiceMessage(req.get(), fuchsia_wlan_mlme_MLMEAssociateReqOrdinal, out_msg);
}

zx_status_t CreateAssocResponse(MlmeMsg<wlan_mlme::AssociateResponse>* out_msg,
                                wlan_mlme::AssociateResultCodes result_code) {
    common::MacAddr client(kClientAddress);

    auto resp = wlan_mlme::AssociateResponse::New();
    std::memcpy(resp->peer_sta_address.mutable_data(), client.byte, common::kMacAddrLen);
    resp->result_code = result_code;
    resp->association_id = kAid;

    return WriteServiceMessage(resp.get(), fuchsia_wlan_mlme_MLMEAssociateRespOrdinal, out_msg);
}

zx_status_t CreateEapolRequest(MlmeMsg<wlan_mlme::EapolRequest>* out_msg) {
    common::MacAddr bssid(kBssid1);
    common::MacAddr client(kClientAddress);

    auto req = wlan_mlme::EapolRequest::New();
    std::memcpy(req->dst_addr.mutable_data(), client.byte, common::kMacAddrLen);
    std::memcpy(req->src_addr.mutable_data(), bssid.byte, common::kMacAddrLen);
    std::vector<uint8_t> eapol_pdu(kEapolPdu, kEapolPdu + sizeof(kEapolPdu));
    req->data.reset(std::move(eapol_pdu));

    return WriteServiceMessage(req.get(), fuchsia_wlan_mlme_MLMEEapolReqOrdinal, out_msg);
}

zx_status_t CreateSetKeysRequest(MlmeMsg<wlan_mlme::SetKeysRequest>* out_msg,
                                 std::vector<uint8_t> key_data, wlan_mlme::KeyType key_type) {
    wlan_mlme::SetKeyDescriptor key;
    key.key.reset(key_data);
    key.key_id = 1;
    key.key_type = key_type;
    std::memcpy(key.address.mutable_data(), kClientAddress, sizeof(kClientAddress));
    std::memcpy(key.cipher_suite_oui.mutable_data(), kCipherOui, sizeof(kCipherOui));
    key.cipher_suite_type = kCipherSuiteType;

    std::vector<wlan_mlme::SetKeyDescriptor> keylist;
    keylist.emplace_back(std::move(key));
    auto req = wlan_mlme::SetKeysRequest::New();
    req->keylist.reset(std::move(keylist));

    return WriteServiceMessage(req.get(), fuchsia_wlan_mlme_MLMESetKeysReqOrdinal, out_msg);
}

zx_status_t CreateBeaconFrame(fbl::unique_ptr<Packet>* out_packet) {
    return CreateBeaconFrameWithBssid(out_packet, common::MacAddr(kBssid1));
}

zx_status_t CreateBeaconFrameWithBssid(fbl::unique_ptr<Packet>* out_packet, common::MacAddr bssid) {
    size_t body_payload_len = 256;
    MgmtFrame<Beacon> frame;
    auto status = CreateMgmtFrame(&frame, body_payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = common::kBcastMac;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    frame.FillTxInfo();

    auto bcn = frame.body();
    bcn->beacon_interval = kBeaconPeriodTu;
    bcn->timestamp = 0;
    bcn->cap.set_ess(1);
    bcn->cap.set_short_preamble(1);

    ElementWriter w(bcn->elements, body_payload_len);
    if (WriteSsid(&w, kSsid, sizeof(kSsid)) != ZX_OK) { return ZX_ERR_IO; }

    std::vector<SupportedRate> rates(std::cbegin(kSupportedRates), std::cend(kSupportedRates));
    if (WriteSupportedRates(&w, rates) != ZX_OK) { return ZX_ERR_IO; }

    if (WriteDsssParamSet(&w, kBssChannel) != ZX_OK) { return ZX_ERR_IO; }

    if (WriteCountry(&w, kBssChannel) != ZX_OK) { return ZX_ERR_IO; }

    std::vector<SupportedRate> ext_rates(std::cbegin(kExtendedSupportedRates),
                                         std::cend(kExtendedSupportedRates));
    if (WriteExtendedSupportedRates(&w, ext_rates) != ZX_OK) { return ZX_ERR_IO; }

    ZX_DEBUG_ASSERT(bcn->Validate(w.size()));
    size_t body_len = bcn->len() + w.size();
    if (frame.set_body_len(body_len) != ZX_OK) { return ZX_ERR_IO; }

    auto pkt = frame.Take();
    wlan_rx_info_t rx_info{.rx_flags = 0};
    pkt->CopyCtrlFrom(rx_info);

    *out_packet = fbl::move(pkt);

    return ZX_OK;
}

zx_status_t CreateAuthReqFrame(fbl::unique_ptr<Packet>* out_packet) {
    common::MacAddr bssid(kBssid1);
    common::MacAddr client(kClientAddress);

    MgmtFrame<Authentication> frame;
    auto status = CreateMgmtFrame(&frame);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = bssid;
    hdr->addr2 = client;
    hdr->addr3 = bssid;
    frame.FillTxInfo();

    auto auth = frame.body();
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    auth->auth_txn_seq_number = 1;
    auth->status_code = 0;  // Reserved: explicitly set to 0

    auto pkt = frame.Take();
    wlan_rx_info_t rx_info{.rx_flags = 0};
    pkt->CopyCtrlFrom(rx_info);

    *out_packet = fbl::move(pkt);

    return ZX_OK;
}

zx_status_t CreateAuthRespFrame(fbl::unique_ptr<Packet>* out_packet) {
    common::MacAddr bssid(kBssid1);
    common::MacAddr client(kClientAddress);

    MgmtFrame<Authentication> frame;
    auto status = CreateMgmtFrame(&frame);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = client;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    frame.FillTxInfo();

    auto auth = frame.body();
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    auth->auth_txn_seq_number = 2;
    auth->status_code = status_code::kSuccess;

    auto pkt = frame.Take();
    wlan_rx_info_t rx_info{.rx_flags = 0};
    pkt->CopyCtrlFrom(rx_info);

    *out_packet = fbl::move(pkt);

    return ZX_OK;
}

zx_status_t CreateAssocReqFrame(fbl::unique_ptr<Packet>* out_packet) {
    common::MacAddr bssid(kBssid1);
    common::MacAddr client(kClientAddress);

    // arbitrarily large reserved len; will shrink down later
    size_t reserved_len = 4096;
    MgmtFrame<AssociationRequest> frame;
    auto status = CreateMgmtFrame(&frame, reserved_len);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = bssid;
    hdr->addr2 = client;
    hdr->addr3 = bssid;
    frame.FillTxInfo();

    auto assoc = frame.body();
    CapabilityInfo cap = {};
    cap.set_short_preamble(1);
    cap.set_ess(1);
    assoc->cap = cap;
    assoc->listen_interval = kListenInterval;

    size_t elems_len = sizeof(SsidElement) + sizeof(kSsid) + sizeof(kRsne);
    ElementWriter w(assoc->elements, elems_len);
    if (!w.write<SsidElement>(kSsid, sizeof(kSsid))) { return ZX_ERR_IO; }
    if (!w.write<RsnElement>(kRsne, sizeof(kRsne))) { return ZX_ERR_IO; }

    frame.set_body_len(sizeof(AssociationRequest) + elems_len);
    auto pkt = frame.Take();
    wlan_rx_info_t rx_info{.rx_flags = 0};
    pkt->CopyCtrlFrom(rx_info);

    *out_packet = fbl::move(pkt);

    return ZX_OK;
}

zx_status_t CreateAssocRespFrame(fbl::unique_ptr<Packet>* out_packet) {
    common::MacAddr bssid(kBssid1);
    common::MacAddr client(kClientAddress);

    MgmtFrame<AssociationResponse> frame;
    auto status = CreateMgmtFrame(&frame);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = client;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    frame.FillTxInfo();

    auto assoc = frame.body();
    assoc->aid = kAid;
    CapabilityInfo cap = {};
    cap.set_short_preamble(1);
    cap.set_ess(1);
    assoc->cap = cap;
    assoc->status_code = status_code::kSuccess;

    auto pkt = frame.Take();
    wlan_rx_info_t rx_info{.rx_flags = 0};
    pkt->CopyCtrlFrom(rx_info);

    *out_packet = fbl::move(pkt);

    return ZX_OK;
}

DataFrame<LlcHeader> CreateDataFrame(const uint8_t* payload, size_t len) {
    common::MacAddr bssid(kBssid1);
    common::MacAddr client(kClientAddress);

    const size_t buf_len = DataFrameHeader::max_len() + LlcHeader::max_len() + len;
    auto packet = GetWlanPacket(buf_len);
    if (packet == nullptr) { return {}; }

    wlan_rx_info_t rx_info{.rx_flags = 0};
    packet->CopyCtrlFrom(rx_info);

    DataFrame<LlcHeader> data_frame(fbl::move(packet));
    auto data_hdr = data_frame.hdr();
    std::memset(data_hdr, 0, DataFrameHeader::max_len());
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(DataSubtype::kDataSubtype);
    data_hdr->fc.set_from_ds(1);
    data_hdr->addr1 = bssid;
    data_hdr->addr2 = bssid;
    data_hdr->addr3 = client;
    data_hdr->sc.set_val(42);

    auto llc_hdr = data_frame.body();
    llc_hdr->dsap = kLlcSnapExtension;
    llc_hdr->ssap = kLlcSnapExtension;
    llc_hdr->control = kLlcUnnumberedInformation;
    std::memcpy(llc_hdr->oui, kLlcOui, sizeof(llc_hdr->oui));
    llc_hdr->protocol_id = 42;
    if (len > 0) { std::memcpy(llc_hdr->payload, payload, len); }

    size_t actual_body_len = llc_hdr->len() + len;
    auto status = data_frame.set_body_len(actual_body_len);
    if (status != ZX_OK) { return {}; }

    return data_frame;
}

DataFrame<> CreateNullDataFrame() {
    common::MacAddr bssid(kBssid1);
    common::MacAddr client(kClientAddress);

    auto packet = GetWlanPacket(DataFrameHeader::max_len());
    if (packet == nullptr) { return {}; }

    wlan_rx_info_t rx_info{.rx_flags = 0};
    packet->CopyCtrlFrom(rx_info);

    DataFrame<> data_frame(fbl::move(packet));
    auto data_hdr = data_frame.hdr();
    std::memset(data_hdr, 0, DataFrameHeader::max_len());
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(DataSubtype::kNull);
    data_hdr->fc.set_from_ds(1);
    data_hdr->addr1 = client;
    data_hdr->addr2 = bssid;
    data_hdr->addr3 = bssid;
    data_hdr->sc.set_val(42);
    return data_frame;
}

EthFrame CreateEthFrame(const uint8_t* payload, size_t len) {
    common::MacAddr bssid(kBssid1);
    common::MacAddr client(kClientAddress);

    size_t buf_len = EthernetII::max_len() + len;
    auto packet = GetEthPacket(buf_len);
    if (packet == nullptr) { return {}; }

    EthFrame eth_frame(fbl::move(packet));
    auto eth_hdr = eth_frame.hdr();
    std::memset(eth_hdr, 0, buf_len);
    eth_hdr->src = client;
    eth_hdr->dest = bssid;
    eth_hdr->ether_type = 2;
    std::memcpy(eth_hdr->payload, payload, len);

    return eth_frame;
}

}  // namespace wlan
