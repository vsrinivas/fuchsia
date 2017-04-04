// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <magenta/assert.h>
#include <magenta/syscalls/port.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#define DEBUG 0

#define logf(args...) do { \
    if (DEBUG) std::printf("wlan: " args); \
} while (false)

#define logfn() logf("%s\n", __func__)


namespace wlan {

#define WLAN_DEV(d) static_cast<Device*>(d)

Device::Device(mx_driver_t* driver, mx_device_t* device, wlanmac_protocol_t* wlanmac_ops)
  : driver_(driver), wlanmac_device_(device), wlanmac_ops_(wlanmac_ops) {
    logfn();
    MX_DEBUG_ASSERT(driver_ && wlanmac_device_ && wlanmac_ops_);
}

Device::~Device() {
    logfn();
}

mx_status_t Device::Bind() {
    logfn();

    device_ops_.unbind = [](mx_device_t* dev){ WLAN_DEV(dev->ctx)->Unbind(); };
    device_ops_.release = [](mx_device_t* dev) { return WLAN_DEV(dev->ctx)->Release(); };
    device_init(&device_, driver_, "wlan", &device_ops_);

    auto status = wlanmac_ops_->query(wlanmac_device_, 0u, &ethmac_info_);
    if (status != NO_ERROR) {
        logf("warning: could not query wlan mac device: %d\n", status);
    }

    ethmac_ops_.query = [](mx_device_t* dev, uint32_t options, ethmac_info_t* info) {
        return WLAN_DEV(dev->ctx)->Query(options, info);
    };
    ethmac_ops_.stop = [](mx_device_t* dev) { WLAN_DEV(dev->ctx)->Stop(); };
    ethmac_ops_.start = [](mx_device_t* dev, ethmac_ifc_t* ifc, void* cookie) {
        return WLAN_DEV(dev->ctx)->Start(ifc, cookie);
    };
    ethmac_ops_.send = [](mx_device_t* dev, uint32_t options, void* data, size_t length) {
        WLAN_DEV(dev->ctx)->Send(options, data, length);
    };

    wlanmac_ifc_.status = [](void* cookie, uint32_t status) { WLAN_DEV(cookie)->MacStatus(status); };
    wlanmac_ifc_.recv = [](void* cookie, void* data, size_t length, uint32_t flags) {
        WLAN_DEV(cookie)->Recv(data, length, flags);
    };

    device_.ctx = this;
    device_.protocol_id = MX_PROTOCOL_ETHERMAC;
    device_.protocol_ops = &ethmac_ops_;

    status = mx::port::create(MX_PORT_OPT_V2, &port_);
    if (status != NO_ERROR) {
        logf("could not create port: %d\n", status);
        return status;
    }
    work_thread_ = std::thread(&Device::MainLoop, this);

    status = device_add(&device_, wlanmac_device_);
    if (status != NO_ERROR) {
        logf("could not add device err=%d\n", status);
    } else {
        logf("device added\n");
    }

    return status;
}

void Device::Unbind() {
    logfn();
    device_remove(&device_);
}

mx_status_t Device::Release() {
    logfn();
    if (port_.is_valid()) {
        auto status = SendShutdown();
        if (status != NO_ERROR) {
            MX_PANIC("wlan: could not send shutdown loop message: %d\n", status);
        }
        if (work_thread_.joinable()) {
            work_thread_.join();
        }
    }
    delete this;
    return NO_ERROR;
}

mx_status_t Device::Query(uint32_t options, ethmac_info_t* info) {
    logfn();
    if (info == nullptr) return ERR_INVALID_ARGS;
    // Make sure this device is reported as a wlan device
    info->features = ethmac_info_.features | ETHMAC_FEATURE_WLAN;
    info->mtu = ethmac_info_.mtu;
    std::memcpy(info->mac, ethmac_info_.mac, ETH_MAC_SIZE);
    return NO_ERROR;
}

mx_status_t Device::Start(ethmac_ifc_t* ifc, void* cookie) {
    logfn();
    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_ifc_ != nullptr) {
        return ERR_ALREADY_BOUND;
    }
    ethmac_ifc_ = ifc;
    ethmac_cookie_ = cookie;
    auto status = wlanmac_ops_->start(wlanmac_device_, &wlanmac_ifc_, this);
    if (status != NO_ERROR) {
        logf("could not start wlanmac: %d\n", status);
        ethmac_ifc_ = nullptr;
        ethmac_cookie_ = nullptr;
    }
    return status;
}

void Device::Stop() {
    logfn();
    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_ifc_ == nullptr) {
        logf("warning: ethmac already stopped\n");
    }
    ethmac_ifc_ = nullptr;
    ethmac_cookie_ = nullptr;
}

void Device::Send(uint32_t options, void* data, size_t length) {
    // no logfn() because it's too noisy
}

void Device::MacStatus(uint32_t status) {
    logfn();
}

void Device::Recv(void* data, size_t length, uint32_t flags) {
    // no logfn() because it's too noisy
}

Device::loop_message::loop_message(MsgKey key, uint32_t extra) {
    logf("loop_message key=%" PRIu64 ", extra=%u\n", key, extra);
    hdr.key = key;
    hdr.extra = extra;
}

void Device::MainLoop() {
    logfn();
    mx_port_packet_t pkt;
    while (true) {
        auto status = port_.wait(MX_TIME_INFINITE, &pkt, 0);
        if (status != NO_ERROR) {
            if (status == ERR_BAD_HANDLE) {
                logf("port closed, exiting\n");
            } else {
                logf("error waiting on port: %d\n", status);
            }
            break;
        }

        if (pkt.type == MX_PKT_TYPE_USER) {
            switch (pkt.key) {
            case kShutdown:
                port_.reset();
                break;
            default:
                logf("unknown port key: %" PRIu64 "\n", pkt.key);
                break;
            }
        }
    }
    logf("exiting MainLoop\n");
}

mx_status_t Device::SendShutdown() {
    logfn();
    loop_message msg(kShutdown);
    return port_.queue(&msg, 0);
}

#undef WLAN_DEV

}  // namespace wlan
