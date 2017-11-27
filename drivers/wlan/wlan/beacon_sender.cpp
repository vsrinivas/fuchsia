// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "beacon_sender.h"
#include "logging.h"

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
    (void)device_;
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
                if (IsStartedLocked()) { SendBeaconFrameLocked(); }
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
    // TODO(hahnr): Send Beacon frame.

    SetTimeout();
    return ZX_OK;
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

}  // namespace wlan
