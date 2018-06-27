// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/client_mlme.h>

#include <wlan/common/bitfield.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/client/scanner.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/frame_dispatcher.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/wlan.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;

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

    scanner_.reset(new Scanner(device_, std::move(timer)));
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

zx_status_t ClientMlme::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    // Let the Scanner handle all MLME-SCAN.requests.
    if (auto scan_req = msg.As<wlan_mlme::ScanRequest>()) {
        return scanner_->HandleMlmeScanReq(*scan_req);
    }

    // MLME-JOIN.request will reset the STA.
    if (auto join_req = msg.As<wlan_mlme::JoinRequest>()) {
        HandleMlmeJoinReq(*join_req);
    }

    // Once the STA was spawned, let it handle all incoming MLME messages.
    if (sta_ != nullptr) { DispatchMlmeMsg(msg, sta_.get()); }
    return ZX_OK;
}

zx_status_t ClientMlme::HandleFramePacket(fbl::unique_ptr<Packet> pkt) {
    // TODO(hahnr): Extract into some form of helper method.
    MgmtFrameView<> mgmt_frame(pkt.get());
    if (mgmt_frame.hdr()->fc.IsMgmt()) {
        if (mgmt_frame.hdr()->IsBeacon()) {
            auto bcn = mgmt_frame.Specialize<Beacon>();
            if (bcn.HasValidLen()) { scanner_->HandleBeacon(bcn); }
        } else if (mgmt_frame.hdr()->IsProbeResponse()) {
            auto proberesp = mgmt_frame.Specialize<ProbeResponse>();
            if (proberesp.HasValidLen()) { scanner_->HandleProbeResponse(proberesp); }
        }
    }

    if (sta_ != nullptr) { DispatchFramePacket(fbl::move(pkt), sta_.get()); }
    return ZX_OK;
}

zx_status_t ClientMlme::HandleMlmeJoinReq(const MlmeMsg<wlan_mlme::JoinRequest>& req) {
    debugfn();
    fbl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kStation));
    timer_id.set_mac(common::MacAddr(req.body()->selected_bss.bssid.data()).ToU64());
    auto status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
    if (status != ZX_OK) {
        errorf("could not create station timer: %d\n", status);
        return status;
    }

    sta_.reset(new Station(device_, std::move(timer)));
    return ZX_OK;
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

wlan_stats::MlmeStats ClientMlme::GetMlmeStats() const {
    wlan_stats::MlmeStats mlme_stats{};
    if (sta_) {
      mlme_stats.set_client_mlme_stats(sta_->stats());
    }
    return mlme_stats;
}

}  // namespace wlan
