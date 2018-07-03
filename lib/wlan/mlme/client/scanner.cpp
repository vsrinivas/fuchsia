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
        if (s != ZX_OK) {
            errorf("failed to send OnScanEnd event: %d\n", s);
        }
    } else {
        // TODO(gbonik): remove legacy support
        wlan_mlme::ScanConfirm msg;
        msg.result_code = code;
        msg.bss_description_set.resize(0);
        zx_status_t s = SendServiceMsg(device, &msg, fuchsia_wlan_mlme_MLMEScanConfOrdinal);
        if (s != ZX_OK) {
            errorf("failed to send ScanConf event: %d\n", s);
        }
    }
}

static zx_status_t SendResults(DeviceInterface* device, uint64_t txn_id,
                               const std::unordered_map<uint64_t, Bss>& bss_map) {
    for (auto& p : bss_map) {
        wlan_mlme::ScanResult r;
        r.txn_id = txn_id;
        r.bss = p.second.ToFidl();
        zx_status_t status = SendServiceMsg(device, &r, fuchsia_wlan_mlme_MLMEOnScanResultOrdinal);
        if (status != ZX_OK) {
            return status;
        }
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
    if (status != ZX_OK) {
        errorf("failed to send ScanConf: %d\n", status);
    }
    return status;
}

// TODO(NET-500): The way we handle Beacons and ProbeResponses in here is kinda gross. Refactor.

Scanner::Scanner(DeviceInterface* device, fbl::unique_ptr<Timer> timer)
    : device_(device), timer_(std::move(timer)) {
    ZX_DEBUG_ASSERT(timer_.get());
}

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
    ZX_DEBUG_ASSERT(channel_start_.get() == 0);

    if (req.body()->channel_list->size() == 0
        || req.body()->max_channel_time < req.body()->min_channel_time)
    {
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

    channel_start_ = timer_->Now();
    zx::time timeout = InitialTimeout();
    status = device_->SetChannel(ScanChannel());
    if (status != ZX_OK) {
        errorf("could not queue set channel: %d\n", status);
        SendScanEnd(device_, req.body()->txn_id, wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
        Reset();
        return status;
    }

    status = timer_->SetTimer(timeout);
    if (status != ZX_OK) {
        errorf("could not start scan timer: %d\n", status);
        SendScanEnd(device_, req.body()->txn_id, wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
        Reset();
        return status;
    }

    return ZX_OK;
}

void Scanner::Reset() {
    debugfn();
    req_.reset();
    channel_index_ = 0;
    channel_start_ = zx::time();
    timer_->CancelTimer();
    current_bss_.clear();
}

bool Scanner::IsRunning() const {
    return req_ != nullptr;
}

Scanner::Type Scanner::ScanType() const {
    ZX_DEBUG_ASSERT(IsRunning());
    switch (req_->scan_type) {
    case wlan_mlme::ScanTypes::PASSIVE:
        return Type::kPassive;
    case wlan_mlme::ScanTypes::ACTIVE:
        return Type::kActive;
    }
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
    if (!ShouldDropMgmtFrame(*frame.hdr())) {
        ProcessBeacon(frame);
    }
}

void Scanner::HandleProbeResponse(const MgmtFrameView<ProbeResponse>& frame) {
    debugfn();
    if (!ShouldDropMgmtFrame(*frame.hdr())) {
        // ProbeResponse holds the same fields as a Beacon with the only difference in their IEs.
        // Thus, we can safely convert a ProbeResponse to a Beacon.
        auto bcn_frame = frame.Specialize<Beacon>();
        ProcessBeacon(bcn_frame);
    }
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
        it = current_bss_.emplace(std::piecewise_construct,
            std::forward_as_tuple(bssid.ToU64()),
            std::forward_as_tuple(bssid)).first;
    }

    zx_status_t status = it->second.ProcessBeacon(
        *bcn_frame.body(), bcn_frame.body_len(), bcn_frame.rx_info());
    if (status != ZX_OK) {
        debugbcn("Failed to handle beacon (err %3d): BSSID %s timestamp: %15" PRIu64 "\n", status,
                 MACSTR(bssid), bcn_frame.body()->timestamp);
    }
}

zx_status_t Scanner::HandleTimeout() {
    debugfn();
    ZX_DEBUG_ASSERT(IsRunning());

    zx::time now = timer_->Now();
    zx_status_t status = ZX_OK;

    // Reached max channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->max_channel_time)) {
        // TODO(gbonik): remove the 'if' once we remove legacy support
        if (req_->txn_id != 0) {
            status = SendResults(device_, req_->txn_id, current_bss_);
            if (status != ZX_OK) {
                errorf("scanner: failed to send results: %d\n", status);
                goto fail;
            }
            current_bss_.clear();
        }
        if (++channel_index_ >= req_->channel_list->size()) {
            timer_->CancelTimer();

            // TODO(gbonik): remove legacy support
            if (req_->txn_id != 0) {
                SendScanEnd(device_, req_->txn_id, wlan_mlme::ScanResultCodes::SUCCESS);
            } else {
                SendLegacyScanConf(device_, current_bss_);
            }
            Reset();
            return ZX_OK;
        } else {
            channel_start_ = timer_->Now();
            status = timer_->SetTimer(InitialTimeout());
            if (status != ZX_OK) { goto fail; }
            status = device_->SetChannel(ScanChannel());
            if (status != ZX_OK) {
                errorf("scanner: could not set channel: %d\n", status);
                goto fail;
            }
        }
    }

    // TODO(tkilbourn): can probe delay come after min_channel_time?

    // Reached min channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->min_channel_time)) {
        // TODO(tkilbourn): if there was no sign of activity on this channel, skip ahead to the next
        // one
        // For now, just continue the scan.
        zx::time timeout = channel_start_ + WLAN_TU(req_->max_channel_time);
        status = timer_->SetTimer(timeout);
        if (status != ZX_OK) {
            errorf("could not set scan timer: %d\n", status);
            goto fail;
        }
        return ZX_OK;
    }

    // Reached probe delay for an active scan
    if (req_->scan_type == wlan_mlme::ScanTypes::ACTIVE &&
        now >= channel_start_ + WLAN_TU(req_->probe_delay)) {
        debugf("Reached probe delay\n");
        // TODO(hahnr): Add support for CCA as described in IEEE Std 802.11-2016 11.1.4.3.2 f)
        zx::time timeout = channel_start_ + WLAN_TU(req_->min_channel_time);
        status = timer_->SetTimer(timeout);
        if (status != ZX_OK) {
            errorf("could not set scan timer: %d\n", status);
            goto fail;
        }
        SendProbeRequest();
        return ZX_OK;
    }

    // Haven't reached a timeout yet; continue scanning
    return ZX_OK;

fail:
    SendScanEnd(device_, req_->txn_id, wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
    Reset();
    return status;
}

zx::time Scanner::InitialTimeout() const {
    if (req_->scan_type == wlan_mlme::ScanTypes::PASSIVE) {
        return channel_start_ + WLAN_TU(req_->min_channel_time);
    } else {
        return channel_start_ + WLAN_TU(req_->probe_delay);
    }
}

// TODO(hahnr): support SSID list (IEEE Std 802.11-2016 11.1.4.3.2)
zx_status_t Scanner::SendProbeRequest() {
    debugfn();

    size_t body_payload_len = 128;  // TODO(porce): Revisit this value choice.
    MgmtFrame<ProbeRequest> frame;
    auto status = BuildMgmtFrame(&frame, body_payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    const common::MacAddr& mymac = device_->GetState()->address();
    const common::MacAddr& bssid = common::MacAddr(req_->bssid.data());

    hdr->addr1 = common::kBcastMac;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid;
    // TODO(NET-556): Clarify 'Sequence' ownership of MLME and STA. Don't set sequence number for
    // now.
    hdr->sc.set_seq(0);
    frame.FillTxInfo();

    auto body = frame.body();
    ElementWriter w(body->elements, body_payload_len);

    if (!w.write<SsidElement>(req_->ssid->data())) {
        errorf("could not write ssid \"%s\" to probe request\n", req_->ssid->data());
        return ZX_ERR_IO;
    }

    // TODO(hahnr): determine these rates based on hardware
    // Rates (in Mbps): 1, 2, 5.5, 6, 9, 11, 12, 18
    std::vector<uint8_t> rates = {0x02, 0x04, 0x0b, 0x0c, 0x12, 0x16, 0x18, 0x24};
    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("could not write supported rates\n");
        return ZX_ERR_IO;
    }

    // Rates (in Mbps): 24, 36, 48, 54
    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("could not write extended supported rates\n");
        return ZX_ERR_IO;
    }

    // Validate the request in debug mode
    ZX_DEBUG_ASSERT(body->Validate(w.size()));

    // Update the length with final values
    // TODO(porce): implement methods to replace sizeof(ProbeRequest) with body.some_len()
    size_t body_len = sizeof(ProbeRequest) + w.size();
    status = frame.set_body_len(body_len);
    if (status != ZX_OK) {
        errorf("could not set body length to %zu: %d\n", body_len, status);
        return status;
    }

    status = device_->SendWlan(frame.Take());
    if (status != ZX_OK) { errorf("could not send probe request packet: %d\n", status); }

    return status;
}

}  // namespace wlan
