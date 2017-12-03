// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client_mlme.h"
#include "dispatcher.h"

#include "interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"
#include "scanner.h"
#include "serialize.h"
#include "station.h"
#include "timer.h"
#include "wlan.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zx/time.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

#include "garnet/drivers/wlan/common/bitfield.h"
#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

namespace wlan {

ClientMlme::ClientMlme(DeviceInterface* device) : device_(device) {
    debugfn();
}

ClientMlme::~ClientMlme() {}

zx_status_t ClientMlme::Init() {
    debugfn();

    fbl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kScanner));
    zx_status_t status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
    if (status != ZX_OK) {
        errorf("could not create scan timer: %d\n", status);
        return status;
    }

    ZX_DEBUG_ASSERT(scanner_.get() == nullptr);
    scanner_ = fbl::AdoptRef(new Scanner(device_, std::move(timer)));
    AddChildHandler(scanner_);
    return status;
}

zx_status_t ClientMlme::HandleTimeout(const ObjectId id) {
    switch (id.target()) {
    case to_enum_type(ObjectTarget::kScanner):
        scanner_->HandleTimeout();
        break;
    case to_enum_type(ObjectTarget::kStation):
        // TODO(porce): Fix this crash point. bssid() can be nullptr.
        if (id.mac() != sta_->bssid()->ToU64()) {
            warnf("timeout for unknown bssid: %s (%" PRIu64 ")\n", MACSTR(*(sta_->bssid())),
                  id.mac());
            break;
        }
        sta_->HandleTimeout();
        break;
    default:
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t ClientMlme::HandleMlmeJoinReq(const JoinRequest& req) {
    debugfn();
    fbl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kStation));
    timer_id.set_mac(common::MacAddr(req.selected_bss->bssid.data()).ToU64());
    auto status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
    if (status != ZX_OK) {
        errorf("could not create station timer: %d\n", status);
        return status;
    }

    RemoveChildHandler(sta_);
    sta_ = fbl::AdoptRef(new Station(device_, std::move(timer)));
    AddChildHandler(sta_);
    return ZX_OK;
}

zx_status_t ClientMlme::HandleMlmeDeviceQueryReq(const DeviceQueryRequest& msg) {
    debugfn();
    auto resp = DeviceQueryResponse::New();
    const wlanmac_info_t& info = device_->GetWlanInfo();
    if (info.mac_modes & WLAN_MAC_MODE_STA) {
        resp->modes.push_back(MacMode::STA);
    }
    if (info.mac_modes & WLAN_MAC_MODE_AP) {
        resp->modes.push_back(MacMode::AP);
    }
    for (uint8_t band_idx = 0; band_idx < info.num_bands; band_idx++) {
        const wlan_band_info_t& band_info = info.bands[band_idx];
        auto band = BandCapabilities::New();
        band->basic_rates.resize(0);
        for (size_t rate_idx = 0; rate_idx < sizeof(band_info.basic_rates); rate_idx++) {
            if (band_info.basic_rates[rate_idx] != 0) {
                band->basic_rates.push_back(band_info.basic_rates[rate_idx]);
            }
        }
        const wlan_chan_list_t& chan_list = band_info.supported_channels;
        band->base_frequency = chan_list.base_freq;
        band->channels.resize(0);
        for (size_t chan_idx = 0; chan_idx < sizeof(chan_list.channels); chan_idx++) {
            if (chan_list.channels[chan_idx] != 0) {
                band->channels.push_back(chan_list.channels[chan_idx]);
            }
        }
        resp->bands.push_back(std::move(band));
    }

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::DEVICE_QUERY_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize DeviceQueryResponse: %d\n", status);
        return status;
    }

    return device_->SendService(std::move(packet));
}

bool ClientMlme::IsStaValid() const {
    // TODO(porce): Redefine the notion of the station validity.
    return sta_ != nullptr && sta_->bssid() != nullptr;
}

zx_status_t ClientMlme::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    if (IsStaValid()) { sta_->PreChannelChange(chan); }
    return ZX_OK;
}

zx_status_t ClientMlme::PostChannelChange() {
    debugfn();
    if (IsStaValid()) { sta_->PostChannelChange(); }
    return ZX_OK;
}

}  // namespace wlan
