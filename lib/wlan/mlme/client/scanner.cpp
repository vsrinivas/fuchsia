// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/scanner.h>

#include <wlan/common/logging.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/sequence.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/wlan.h>

#include <fuchsia/wlan/mlme/c/fidl.h>
#include "lib/fidl/cpp/vector.h"

#include <fbl/unique_ptr.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <cinttypes>
#include <utility>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

static constexpr size_t kMaxBssPerChannel = 100;

static void SendScanEnd(DeviceInterface* device, uint64_t txn_id, wlan_mlme::ScanResultCodes code) {
    if (txn_id != 0) {
        wlan_mlme::ScanEnd msg;
        msg.txn_id = txn_id;
        msg.code = code;
        zx_status_t s = SendServiceMsg(device, &msg, fuchsia_wlan_mlme_MLMEOnScanEndOrdinal);
        if (s != ZX_OK) { errorf("failed to send OnScanEnd event: %d\n", s); }
    } else {
        // TODO(gbonik): remove legacy support
        wlan_mlme::ScanConfirm msg;
        msg.result_code = code;
        msg.bss_description_set.resize(0);
        zx_status_t s = SendServiceMsg(device, &msg, fuchsia_wlan_mlme_MLMEScanConfOrdinal);
        if (s != ZX_OK) { errorf("failed to send ScanConf event: %d\n", s); }
    }
}

static zx_status_t SendResults(DeviceInterface* device, uint64_t txn_id,
                               const std::unordered_map<uint64_t, Bss>& bss_map) {
    for (auto& p : bss_map) {
        wlan_mlme::ScanResult r;
        r.txn_id = txn_id;
        r.bss = p.second.ToFidl();
        zx_status_t status = SendServiceMsg(device, &r, fuchsia_wlan_mlme_MLMEOnScanResultOrdinal);
        if (status != ZX_OK) { return status; }
    }
    return ZX_OK;
}

static zx_status_t SendLegacyScanConf(DeviceInterface* device,
                                      const std::unordered_map<uint64_t, Bss>& bss_map) {
    wlan_mlme::ScanConfirm conf;
    conf.result_code = wlan_mlme::ScanResultCodes::SUCCESS;
    conf.bss_description_set.resize(0);
    for (auto& p : bss_map) {
        conf.bss_description_set.push_back(p.second.ToFidl());
    }
    zx_status_t status = SendServiceMsg(device, &conf, fuchsia_wlan_mlme_MLMEScanConfOrdinal);
    if (status != ZX_OK) { errorf("failed to send ScanConf: %d\n", status); }
    return status;
}

// TODO(NET-500): The way we handle Beacons and ProbeResponses in here is kinda gross. Refactor.

Scanner::Scanner(DeviceInterface* device, ChannelScheduler* chan_sched)
    : off_channel_handler_(this), device_(device), chan_sched_(chan_sched)
{ }

zx_status_t Scanner::HandleMlmeScanReq(const MlmeMsg<wlan_mlme::ScanRequest>& req) {
    return Start(req);
}

zx_status_t Scanner::Start(const MlmeMsg<wlan_mlme::ScanRequest>& req) {
    debugfn();

    if (IsRunning()) {
        SendScanEnd(device_, req.body()->txn_id, wlan_mlme::ScanResultCodes::NOT_SUPPORTED);
        return ZX_ERR_UNAVAILABLE;
    }
    ZX_DEBUG_ASSERT(channel_index_ == 0);

    if (req.body()->channel_list->size() == 0 ||
        req.body()->max_channel_time < req.body()->min_channel_time) {
        SendScanEnd(device_, req.body()->txn_id, wlan_mlme::ScanResultCodes::INVALID_ARGS);
        return ZX_ERR_INVALID_ARGS;
    }
    // TODO(NET-629): re-enable checking the enum value after fidl2 lands
    // if (!BSSTypes_IsValidValue(req.body()->bss_type) ||
    // !ScanTypes_IsValidValue(req.body()->scan_type)) {
    //    return SendScanConfirm();
    //}

    req_ = wlan_mlme::ScanRequest::New();
    zx_status_t status = req.body()->Clone(req_.get());
    if (status != ZX_OK) {
        errorf("could not clone Scanrequest: %d\n", status);
        SendScanEnd(device_, req.body()->txn_id, wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
        Reset();
        return status;
    }

    chan_sched_->RequestOffChannelTime(CreateOffChannelRequest());

    return ZX_OK;
}

void Scanner::OffChannelHandlerImpl::BeginOffChannelTime() {
    // Don't do anything for now. For active scans, we should send a probe request,
    // or set a timer to send a probe request.
    // TODO(NET-1294)
}

void Scanner::OffChannelHandlerImpl::HandleOffChannelFrame(fbl::unique_ptr<Packet> pkt) {
    if (auto mgmt_frame = MgmtFrameView<>::CheckType(pkt.get()).CheckLength()) {
        if (auto bcn_frame = mgmt_frame.CheckBodyType<Beacon>().CheckLength()) {
            scanner_->HandleBeacon(bcn_frame);
        }
    }
}

bool Scanner::OffChannelHandlerImpl::EndOffChannelTime(bool interrupted,
                                                       OffChannelRequest* next_req) {
    // If we were interrupted before the timeout ended, scan the channel again
    if (interrupted) {
        *next_req = scanner_->CreateOffChannelRequest();
        return true;
    }

    // TODO(gbonik): remove the 'if' once we remove legacy support
    if (scanner_->req_->txn_id != 0) {
        zx_status_t status = SendResults(scanner_->device_, scanner_->req_->txn_id,
                                         scanner_->current_bss_);
        if (status != ZX_OK) {
            errorf("scanner: failed to send results: %d\n", status);
            SendScanEnd(scanner_->device_, scanner_->req_->txn_id,
                        wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
            scanner_->Reset();
            return false;
        }
        scanner_->current_bss_.clear();
    }
    if (++scanner_->channel_index_ >= scanner_->req_->channel_list->size()) {
        // TODO(gbonik): remove legacy support
        if (scanner_->req_->txn_id != 0) {
            SendScanEnd(scanner_->device_, scanner_->req_->txn_id,
                        wlan_mlme::ScanResultCodes::SUCCESS);
        } else {
            SendLegacyScanConf(scanner_->device_, scanner_->current_bss_);
        }
        scanner_->Reset();
        return false;
    } else {
        *next_req = scanner_->CreateOffChannelRequest();
        return true;
    }
}

void Scanner::Reset() {
    debugfn();
    req_.reset();
    channel_index_ = 0;
    current_bss_.clear();
}

bool Scanner::IsRunning() const {
    return req_ != nullptr;
}

wlan_channel_t Scanner::ScanChannel() const {
    debugfn();
    ZX_DEBUG_ASSERT(IsRunning());
    ZX_DEBUG_ASSERT(channel_index_ < req_->channel_list->size());
    return wlan_channel_t{
        .primary = req_->channel_list->at(channel_index_),
    };
}

bool Scanner::ShouldDropMgmtFrame(const MgmtFrameHeader& hdr) {
    // Ignore all management frames when scanner is not running.
    if (!IsRunning()) { return true; }

    common::MacAddr bssid(hdr.addr3);
    common::MacAddr src_addr(hdr.addr2);
    if (bssid != src_addr) {
        // Undefined situation. Investigate if roaming needs this or this is a plain dark art.
        // Do not process frame.
        debugbcn("Rxed a beacon/probe_resp from the non-BSSID station: BSSID %s   SrcAddr %s\n",
                 MACSTR(bssid), MACSTR(src_addr));
        return true;
    }

    return false;
}

void Scanner::HandleBeacon(const MgmtFrameView<Beacon>& frame) {
    debugfn();
    if (!ShouldDropMgmtFrame(*frame.hdr())) { ProcessBeacon(frame); }
}

void Scanner::ProcessBeacon(const MgmtFrameView<Beacon>& bcn_frame) {
    debugfn();
    auto bssid = bcn_frame.hdr()->addr3;

    auto it = current_bss_.find(bssid.ToU64());
    if (it == current_bss_.end()) {
        if (current_bss_.size() >= kMaxBssPerChannel) {
            errorf("maximum number of BSS per channel reached: %lu\n", current_bss_.size());
            return;
        }
        it = current_bss_
                 .emplace(std::piecewise_construct, std::forward_as_tuple(bssid.ToU64()),
                          std::forward_as_tuple(bssid))
                 .first;
    }

    zx_status_t status =
        it->second.ProcessBeacon(*bcn_frame.body(), bcn_frame.body_len(), bcn_frame.rx_info());
    if (status != ZX_OK) {
        debugbcn("Failed to handle beacon (err %3d): BSSID %s timestamp: %15" PRIu64 "\n", status,
                 MACSTR(bssid), bcn_frame.body()->timestamp);
    }
}

OffChannelRequest Scanner::CreateOffChannelRequest() {
   return OffChannelRequest {
       .chan = ScanChannel(),
       .duration = WLAN_TU(req_->max_channel_time),
       .handler = &off_channel_handler_
   };
}

}  // namespace wlan
