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
}

void Station::Reset() {
    debugfn();

    timer_->CancelTimer();
    state_ = WlanState::kUnjoined;
    bss_.reset();
    join_timeout_ = 0;
    last_seen_ = 0;
}

const uint8_t* Station::bssid() const {
    if (bss_.get() == nullptr) {
        return nullptr;
    }
    return bss_->bssid.data();
}

wlan_channel_t Station::channel() const {
    MX_DEBUG_ASSERT(state_ != WlanState::kUnjoined);
    MX_DEBUG_ASSERT(!bss_.is_null());

    return wlan_channel_t{ bss_->channel };
}

mx_status_t Station::Join(JoinRequestPtr req) {
    debugfn();

    bss_.Swap(std::move(&req->selected_bss));
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

mx_status_t Station::HandleBeacon(const Packet* packet) {
    debugfn();

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    MX_DEBUG_ASSERT(rxinfo);
    auto hdr = packet->field<MgmtFrameHeader>(0);
    if (!MacEquals(bss_->bssid.data(), hdr->addr3)) {
        // Not our beacon
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

mx_status_t Station::HandleTimeout() {
    debugfn();
    mx_time_t now = timer_->Now();
    if (now > join_timeout_) {
        debugf("join timed out; resetting\n");
        Reset();
        return SendJoinResponse();
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

}  // namespace wlan
