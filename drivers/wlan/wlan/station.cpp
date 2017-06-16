// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "station.h"

#include "device_interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"
#include "serialize.h"
#include "timer.h"

#include <cstring>
#include <utility>

namespace wlan {

Station::Station(DeviceInterface* device, mxtl::unique_ptr<Timer> timer)
  : device_(device), timer_(std::move(timer)) {
    (void)device_;
    (void)timer_;
    (void)auth_alg_;
}

void Station::Reset() {
    debugfn();

    timer_->CancelTimer();
    state_ = WlanState::kUnjoined;
    bss_.reset();
    join_timeout_ = 0;
    auth_timeout_ = 0;
    last_seen_ = 0;
}

mx_status_t Station::Join(JoinRequestPtr req) {
    debugfn();

    MX_DEBUG_ASSERT(!req.is_null());

    if (req->selected_bss.is_null()) {
        errorf("bad join request\n");
        // Don't reset because of a bad request. Just send the response.
        return SendJoinResponse();
    }

    if (state_ != WlanState::kUnjoined) {
        warnf("already joined; resetting station\n");
        Reset();
    }

    bss_ = std::move(req->selected_bss);
    address_.set_data(bss_->bssid.data());
    mx_status_t status = device_->SetChannel(wlan_channel_t{bss_->channel});
    if (status != MX_OK) {
        errorf("could not set wlan channel: %d\n", status);
        Reset();
        SendJoinResponse();
        return status;
    }

    join_timeout_ = timer_->Now() + WLAN_TU(bss_->beacon_period * req->join_failure_timeout);
    status = timer_->StartTimer(join_timeout_);
    if (status != MX_OK) {
        errorf("could not set join timer: %d\n", status);
        Reset();
        SendJoinResponse();
    }
    return status;
}

mx_status_t Station::Authenticate(AuthenticateRequestPtr req) {
    debugfn();

    MX_DEBUG_ASSERT(!req.is_null());

    if (bss_.is_null()) {
        return MX_ERR_BAD_STATE;
    }

    // TODO(tkilbourn): better result codes
    if (!bss_->bssid.Equals(req->peer_sta_address)) {
        errorf("cannot authenticate before joining\n");
        return SendAuthResponse(AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kUnjoined) {
        errorf("must join before authenticating\n");
        return SendAuthResponse(AuthenticateResultCodes::REFUSED);
    }
    if (state_ != WlanState::kUnauthenticated) {
        warnf("already authenticated; sending request anyway\n");
    }
    if (req->auth_type != AuthenticationTypes::OPEN_SYSTEM) {
        // TODO(tkilbourn): support other authentication types
        // TODO(tkilbourn): set the auth_alg_ when we support other authentication types
        errorf("only OpenSystem authentication is supported\n");
        return SendAuthResponse(AuthenticateResultCodes::REFUSED);
    }

    // TODO(tkilbourn): actually send the packet
    // TODO(tkilbourn): better size management
    size_t auth_len = sizeof(MgmtFrameHeader) - sizeof(HtControl) + sizeof(Authentication);
    mxtl::unique_ptr<Buffer> buffer = GetBuffer(auth_len);
    if (buffer == nullptr) {
        return MX_ERR_NO_RESOURCES;
    }

    const DeviceAddress& mymac = device_->GetState()->address();
    uint16_t seq = device_->GetState()->next_seq();
    if (seq == last_seq_) {
        // If the sequence number has rolled over and back to the last seq number we sent to this
        // station, increment again.
        // IEEE Std 802.11-2016, 10.3.2.11.2, Table 10-3, Note TR1
        seq = device_->GetState()->next_seq();
    }
    last_seq_ = seq;

    auto packet = mxtl::unique_ptr<Packet>(new Packet(std::move(buffer), auth_len));
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<MgmtFrameHeader>(0);
    hdr->fc.set_type(kManagement);
    hdr->fc.set_subtype(kAuthentication);
    std::memcpy(hdr->addr1, address_.data(), sizeof(hdr->addr1));
    std::memcpy(hdr->addr2, mymac.data(), sizeof(hdr->addr2));
    std::memcpy(hdr->addr3, address_.data(), sizeof(hdr->addr3));
    hdr->sc.set_seq(seq);

    auto auth = packet->mut_field<Authentication>(hdr->size());
    // TODO(tkilbourn): this assumes Open System authentication
    auth->auth_algorithm_number = auth_alg_;
    auth->auth_txn_seq_number = 1;
    auth->status_code = 0;  // Reserved, so set to 0

    mx_status_t status = device_->SendWlan(std::move(packet));
    if (status != MX_OK) {
        errorf("could not send auth packet: %d\n", status);
        SendAuthResponse(AuthenticateResultCodes::REFUSED);
        return status;
    }

    auth_timeout_ = timer_->Now() + WLAN_TU(bss_->beacon_period * req->auth_failure_timeout);
    status = timer_->StartTimer(auth_timeout_);
    if (status != MX_OK) {
        errorf("could not set auth timer: %d\n", status);
        // This is the wrong result code, but we need to define our own codes at some later time.
        SendAuthResponse(AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
        // TODO(tkilbourn): reset the station?
    }
    return status;
}

mx_status_t Station::HandleBeacon(const Packet* packet) {
    debugfn();

    MX_DEBUG_ASSERT(!bss_.is_null());

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    MX_DEBUG_ASSERT(rxinfo);
    auto hdr = packet->field<MgmtFrameHeader>(0);
    if (DeviceAddress(hdr->addr3) != bss_->bssid.data()) {
        // Not our beacon -- this shouldn't happen because the Mlme should not have routed this
        // packet to this Station.
        MX_DEBUG_ASSERT(false);
        return MX_ERR_BAD_STATE;
    }

    // TODO(tkilbourn): update any other info (like rolling average of rssi)
    last_seen_ = timer_->Now();

    if (join_timeout_ > 0) {
        join_timeout_ = 0;
        timer_->CancelTimer();
        state_ = WlanState::kUnauthenticated;
        debugf("joined %s\n", bss_->ssid.data());
        return SendJoinResponse();
    }

    return MX_OK;
}

mx_status_t Station::HandleAuthentication(const Packet* packet) {
    debugfn();

    if (state_ != WlanState::kUnauthenticated) {
        // TODO(tkilbourn): should we process this Authentication packet anyway? The spec is
        // unclear.
        debugf("unexpected authentication frame\n");
        return MX_OK;
    }

    auto hdr = packet->field<MgmtFrameHeader>(0);
    MX_DEBUG_ASSERT(hdr->fc.subtype() == ManagementSubtype::kAuthentication);
    MX_DEBUG_ASSERT(DeviceAddress(hdr->addr3) == bss_->bssid.data());

    auto auth = packet->field<Authentication>(hdr->size());
    if (!auth) {
        errorf("authentication packet too small (len=%zd)\n", packet->len() - hdr->size());
        return MX_ERR_IO;
    }

    if (auth->auth_algorithm_number != auth_alg_) {
        errorf("mismatched authentication algorithm (expected %u, got %u)\n",
                auth_alg_, auth->auth_algorithm_number);
        return MX_ERR_BAD_STATE;
    }

    // TODO(tkilbourn): this only makes sense for Open System.
    if (auth->auth_txn_seq_number != 2) {
        errorf("unexpected auth txn sequence number (expected 2, got %u)\n",
                auth->auth_txn_seq_number);
        return MX_ERR_BAD_STATE;
    }

    if (auth->status_code != status_code::kSuccess) {
        errorf("authentication failed (status code=%u)\n", auth->status_code);
        // TODO(tkilbourn): is this the right result code?
        SendAuthResponse(AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return MX_ERR_BAD_STATE;
    }

    state_ = WlanState::kAuthenticated;
    auth_timeout_ = 0;
    timer_->CancelTimer();
    SendAuthResponse(AuthenticateResultCodes::SUCCESS);
    return MX_OK;
}

mx_status_t Station::HandleTimeout() {
    debugfn();
    mx_time_t now = timer_->Now();
    if (join_timeout_ > 0 && now > join_timeout_) {
        debugf("join timed out; resetting\n");

        Reset();
        return SendJoinResponse();
    }

    if (auth_timeout_ > 0 && now >= auth_timeout_) {
        infof("auth timed out; moving back to joining\n");
        auth_timeout_ = 0;
        return SendAuthResponse(AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
    }

    return MX_OK;
}

mx_status_t Station::SendJoinResponse() {
    debugfn();
    auto resp = JoinResponse::New();
    resp->result_code = state_ == WlanState::kUnjoined ?
                        JoinResultCodes::JOIN_FAILURE_TIMEOUT :
                        JoinResultCodes::SUCCESS;

    size_t buf_len = sizeof(Header) + resp->GetSerializedSize();
    mxtl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) {
        return MX_ERR_NO_RESOURCES;
    }

    auto packet = mxtl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    mx_status_t status = SerializeServiceMsg(packet.get(), Method::JOIN_confirm, resp);
    if (status != MX_OK) {
        errorf("could not serialize JoinResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

mx_status_t Station::SendAuthResponse(AuthenticateResultCodes code) {
    debugfn();
    auto resp = AuthenticateResponse::New();
    resp->peer_sta_address = fidl::Array<uint8_t>::New(DeviceAddress::kSize);
    std::memcpy(resp->peer_sta_address.data(), bss_->bssid.data(), DeviceAddress::kSize);
    // TODO(tkilbourn): set this based on the actual auth type
    resp->auth_type = AuthenticationTypes::OPEN_SYSTEM;
    resp->result_code = code;

    size_t buf_len = sizeof(Header) + resp->GetSerializedSize();
    mxtl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) {
        return MX_ERR_NO_RESOURCES;
    }

    auto packet = mxtl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    mx_status_t status = SerializeServiceMsg(packet.get(), Method::AUTHENTICATE_confirm, resp);
    if (status != MX_OK) {
        errorf("could not serialize AuthenticateResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

}  // namespace wlan
