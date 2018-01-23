// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "beacon_sender.h"
#include "logging.h"
#include "packet.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zx/thread.h>

#include <chrono>
#include <cinttypes>
#include <future>
#include <iostream>
#include <thread>

namespace wlan {

BeaconSender::BeaconSender(DeviceInterface* device) : device_(device) {
    debugfn();
}

zx_status_t BeaconSender::Init() {
    // Create port.
    auto status = zx::port::create(0, &port_);
    if (status != ZX_OK) {
        errorf("[bcn-sender] could not create port: %d\n", status);
        return status;
    }

    // Create timer.
    zx::timer t;
    status = zx::timer::create(0u, ZX_CLOCK_MONOTONIC, &t);
    if (status != ZX_OK) {
        errorf("[bcn-sender] could not create timer: %d\n", status);
        return status;
    }

    status = t.wait_async(port_, kPortPktKeyTimer, ZX_TIMER_SIGNALED, ZX_WAIT_ASYNC_REPEATING);
    if (status != ZX_OK) {
        errorf("[bcn-sender] could not create timer: %d\n", status);
        return status;
    }
    timer_.reset(new SystemTimer(kPortPktKeyTimer, std::move(t)));
    return ZX_OK;
}

zx_status_t BeaconSender::Start(const StartRequest& req) {
    debugfn();
    std::lock_guard<std::mutex> lock(start_req_lock_);
    ZX_DEBUG_ASSERT(!IsStartedLocked());

    start_req_ = req.Clone();
    bcn_thread_ = std::thread(&BeaconSender::MessageLoop, this);
    started_at_ = std::chrono::steady_clock::now();
    return SetTimeout();
}

zx_status_t BeaconSender::Stop() {
    debugfn();

    // Cancel Timer and destroy MLME-START.request.
    {
        std::lock_guard<std::mutex> lock(start_req_lock_);
        ZX_DEBUG_ASSERT(IsStartedLocked());

        timer_->CancelTimer();
        start_req_.reset();
    }

    // Shutdown thread and wait for its termination.
    zx_port_packet_t pkt = {
        .key = kPortPktKeyShutdown,
        .type = ZX_PKT_TYPE_USER,
    };
    port_.queue(&pkt, 0);
    if (bcn_thread_.joinable()) { bcn_thread_.join(); }

    debugbcnsndr("stopped loop\n");
    return ZX_OK;
}

bool BeaconSender::IsStarted() {
    debugfn();
    std::lock_guard<std::mutex> lock(start_req_lock_);
    return IsStartedLocked();
}

bool BeaconSender::IsStartedLocked() const {
    debugfn();
    return !start_req_.is_null();
}

void BeaconSender::MessageLoop() {
    debugbcnsndr("starting loop\n");
    const char kThreadName[] = "wlan-beacon-sender";
    zx::thread::self().set_property(ZX_PROP_NAME, kThreadName, sizeof(kThreadName));
    // TODO(hahnr): Change to high priority Thread if necessary. Needs evaluation.

    zx_port_packet_t pkt;
    bool running = true;
    while (running) {
        zx_time_t timeout = zx::deadline_after(ZX_SEC(kMessageLoopMaxWaitSeconds));
        zx_status_t status = port_.wait(timeout, &pkt, 0);
        if (status == ZX_ERR_TIMED_OUT) {
            continue;
        } else if (status != ZX_OK) {
            if (status == ZX_ERR_BAD_HANDLE) {
                errorf("[bcn-sender] port closed, exiting loop\n");
            } else {
                errorf("[bcn-sender] error waiting on port: %d\n", status);
            }
            // No further clean-up required. The internal state of the BeaconSender is opaque to
            // its owner. If the thread was terminated the owner should still call Stop().
            break;
        }

        switch (pkt.type) {
        case ZX_PKT_TYPE_USER:
            switch (pkt.key) {
            case kPortPktKeyShutdown:
                debugbcnsndr("shutting down loop\n");
                running = false;
                break;
            default:
                errorf("[bcn-sender] unknown user port key: %" PRIu64 "\n", pkt.key);
                break;
            }
            break;
        case ZX_PKT_TYPE_SIGNAL_REP:
            switch (pkt.key) {
            case kPortPktKeyTimer: {
                std::lock_guard<std::mutex> lock(start_req_lock_);
                if (IsStartedLocked()) {
                    status = SendBeaconFrameLocked();
                    if (status != ZX_OK) {
                        errorf("error sending beacon, exiting message loop: %d\n", status);
                        running = false;
                        break;
                    }
                }
                break;
            }
            default:
                errorf("[bcn-sender] unknown signal port key: %" PRIu64 "\n", pkt.key);
                break;
            }
            break;
        default:
            errorf("[bcn-sender] unknown port packet type: %u\n", pkt.type);
            break;
        }
    }

    debugbcnsndr("stopping loop\n");
}

zx_status_t BeaconSender::SendBeaconFrameLocked() {
    debugfn();
    ZX_DEBUG_ASSERT(IsStartedLocked());
    debugbcnsndr("sending Beacon\n");

    // TODO(hahnr): Length of elements is not known at this time. Allocate enough bytes.
    // This should be updated once there is a better size management.
    size_t body_payload_len = 128;
    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Beacon>(&packet, body_payload_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    const common::MacAddr& bssid = device_->GetState()->address();
    hdr->addr1 = common::kBcastMac;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    hdr->sc.set_seq(next_seq());
    FillTxInfo(&packet, *hdr);

    auto bcn = frame.body;
    bcn->beacon_interval = start_req_->beacon_period;
    bcn->timestamp = beacon_timestamp();
    bcn->cap.set_ess(1);
    bcn->cap.set_short_preamble(1);

    // Write elements.
    // TODO(hahnr): All of this is hardcoded for now. Replace with actual capabilities.
    ElementWriter w(bcn->elements, body_payload_len);
    if (!w.write<SsidElement>(start_req_->ssid.data())) {
        errorf("could not write ssid \"%s\" to Beacon\n", start_req_->ssid.data());
        return ZX_ERR_IO;
    }

    // Rates (in Mbps): 1, 2, 5.5, 6 (base), 9, 11, 12, 18
    std::vector<uint8_t> rates = {0x02, 0x04, 0x0b, 0x8c, 0x12, 0x16, 0x18, 0x24};
    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("[bcn-sender] could not write supported rates\n");
        return ZX_ERR_IO;
    }

    // TODO(hahnr): Replace hardcoded channel.
    if (!w.write<DsssParamSetElement>(1)) {
        errorf("[bcn-sender] could not write extended supported rates\n");
        return ZX_ERR_IO;
    }

    // Rates (in Mbps): 24, 36, 48, 54
    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("[bcn-sender] could not write extended supported rates\n");
        return ZX_ERR_IO;
    }

    // Validate the request in debug mode.
    ZX_DEBUG_ASSERT(bcn->Validate(w.size()));

    // Update the length with final values
    body_payload_len = w.size();
    size_t actual_len = hdr->len() + sizeof(Beacon) + body_payload_len;
    auto status = packet->set_len(actual_len);
    if (status != ZX_OK) {
        errorf("[bcn-sender] could not set packet length to %zu: %d\n", actual_len, status);
        return status;
    }

    status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("[bcn-sender] could not send beacon packet: %d\n", status);
        return status;
    }

    return SetTimeout();
}

// TODO(hahnr): Once InfraBss is submitted, retrieve the timestamp from the BSS.
uint64_t BeaconSender::beacon_timestamp() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - started_at_).count();
}

zx_status_t BeaconSender::SetTimeout() {
    debugfn();
    ZX_DEBUG_ASSERT(timer_);

    timer_->CancelTimer();
    zx_time_t timeout = timer_->Now() + WLAN_TU(start_req_->beacon_period);
    auto status = timer_->SetTimer(timeout);
    if (status != ZX_OK) {
        timer_->CancelTimer();
        errorf("[bcn-sender] could not set timer: %d\n", status);
        return status;
    }
    return ZX_OK;
}

// TODO(hahnr): Once InfraBss is submitted, retrieve the next sequence no from the BSS.
uint16_t BeaconSender::next_seq() {
    uint16_t seq = device_->GetState()->next_seq();
    if (seq == last_seq_) {
        // If the sequence number has rolled over and back to the last seq number we sent,
        // increment again.
        // IEEE Std 802.11-2016, 10.3.2.11.2, Table 10-3, Note TR1
        seq = device_->GetState()->next_seq();
    }
    last_seq_ = seq;
    return seq;
}

}  // namespace wlan
