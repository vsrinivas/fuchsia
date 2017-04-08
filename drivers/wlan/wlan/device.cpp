// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"
#include "logging.h"

#include <magenta/assert.h>
#include <magenta/device/wlan.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>


namespace wlan {

Device::Device(mx_driver_t* driver, mx_device_t* device, wlanmac_protocol_t* wlanmac_ops)
  : WlanBaseDevice("wlan", driver),
    wlanmac_proxy_(wlanmac_ops, device) {
    debugfn();
}

Device::~Device() {
    debugfn();
    MX_DEBUG_ASSERT(!work_thread_.joinable());
}

mx_status_t Device::Bind() {
    debugfn();

    auto status = wlanmac_proxy_.Query(0, &ethmac_info_);
    if (status != NO_ERROR) {
        warnf("could not query wlan mac device: %d\n", status);
    }

    status = mx::port::create(MX_PORT_OPT_V2, &port_);
    if (status != NO_ERROR) {
        errorf("could not create port: %d\n", status);
        return status;
    }
    work_thread_ = std::thread(&Device::MainLoop, this);

    status = Add(wlanmac_proxy_.device());
    if (status != NO_ERROR) {
        errorf("could not add device err=%d\n", status);
        auto shutdown_status = SendShutdown();
        if (shutdown_status != NO_ERROR) {
            MX_PANIC("wlan: could not send shutdown loop message: %d\n", shutdown_status);
        }
        if (work_thread_.joinable()) {
            work_thread_.join();
        }
    } else {
        debugf("device added\n");
    }

    return status;
}

void Device::DdkUnbind() {
    debugfn();
    device_remove(mxdev());
}

void Device::DdkRelease() {
    debugfn();
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
}

mx_status_t Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    debugfn();
    if (op != IOCTL_WLAN_GET_CHANNEL) {
        return ERR_NOT_SUPPORTED;
    }
    if (out_buf == nullptr || out_actual == nullptr || out_len < sizeof(mx_handle_t)) {
        return ERR_BUFFER_TOO_SMALL;
    }

    mx::channel out;
    auto status = GetChannel(&out);
    if (status != NO_ERROR) {
        return status;
    }

    mx_handle_t* outh = static_cast<mx_handle_t*>(out_buf);
    *outh = out.release();
    *out_actual = sizeof(mx_handle_t);
    return NO_ERROR;
}

mx_status_t Device::EthmacQuery(uint32_t options, ethmac_info_t* info) {
    debugfn();
    if (info == nullptr) return ERR_INVALID_ARGS;

    // Make sure this device is reported as a wlan device
    info->features = ethmac_info_.features | ETHMAC_FEATURE_WLAN;
    info->mtu = ethmac_info_.mtu;
    std::memcpy(info->mac, ethmac_info_.mac, ETH_MAC_SIZE);
    return NO_ERROR;
}

mx_status_t Device::EthmacStart(mxtl::unique_ptr<ddk::EthmacIfcProxy> proxy) {
    debugfn();
    MX_DEBUG_ASSERT(proxy != nullptr);

    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_proxy_ != nullptr) {
        return ERR_ALREADY_BOUND;
    }

    auto status = wlanmac_proxy_.Start(this);
    if (status != NO_ERROR) {
        errorf("could not start wlanmac: %d\n", status);
    } else {
        ethmac_proxy_.swap(proxy);
        // TODO: queue a packet on the port to signal the start?
    }
    return status;
}

void Device::EthmacStop() {
    debugfn();

    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_proxy_ == nullptr) {
        warnf("ethmac not started\n");
    }
    ethmac_proxy_.reset();
    // TODO: queue a packet on the port to signal the stop
}

void Device::EthmacSend(uint32_t options, void* data, size_t length) {
    // no debugfn() because it's too noisy
}

void Device::WlanmacStatus(uint32_t status) {
    debugf("WlanmacStatus %u\n", status);
}

void Device::WlanmacRecv(void* data, size_t length, uint32_t flags) {
    // no debugfn() because it's too noisy
}

Device::LoopMessage::LoopMessage(MsgKey key, uint32_t extra) {
    debugf("LoopMessage key=%" PRIu64 ", extra=%u\n", key, extra);
    hdr.key = key;
    hdr.extra = extra;
}

void Device::MainLoop() {
    infof("starting MainLoop\n");
    mx_port_packet_t pkt;
    uint64_t this_key = reinterpret_cast<uint64_t>(this);
    bool running = true;
    while (running) {
        auto status = port_.wait(MX_TIME_INFINITE, &pkt, 0);
        std::lock_guard<std::mutex> lock(lock_);
        if (status != NO_ERROR) {
            if (status == ERR_BAD_HANDLE) {
                debugf("port closed, exiting\n");
            } else {
                errorf("error waiting on port: %d\n", status);
            }
            break;
        }

        switch (pkt.type) {
        case MX_PKT_TYPE_USER:
            switch (pkt.key) {
            case kShutdown:
                running = false;
                continue;
            default:
                errorf("unknown user port key: %" PRIu64 "\n", pkt.key);
                break;
            }
            break;
        case MX_PKT_TYPE_SIGNAL_ONE:
            if (pkt.key != this_key) {
                errorf("unknown signal key: %" PRIu64 ", this_key=%" PRIu64 "\n",
                        pkt.key, this_key);
                break;
            }
            ProcessChannelPacketLocked(pkt);
            break;
        default:
            errorf("unknown port packet type: %u\n", pkt.type);
            break;
        }
    }

    infof("exiting MainLoop\n");
    std::lock_guard<std::mutex> lock(lock_);
    ethmac_proxy_.reset();
    port_.reset();
    channel_.reset();
}

void Device::ProcessChannelPacketLocked(const mx_port_packet_t& pkt) {
    debugf("%s pkt{key=%" PRIu64 ", type=%u, status=%d\n",
            __func__, pkt.key, pkt.type, pkt.status);
    const auto& sig = pkt.signal;
    debugf("signal trigger=%u observed=%u count=%" PRIu64 "\n", sig.trigger, sig.observed,
            sig.count);
    if (sig.observed & MX_CHANNEL_PEER_CLOSED) {
        infof("channel closed\n");
        channel_.reset();
    } else if (sig.observed & MX_CHANNEL_READABLE) {
        uint8_t buf[1024];  // how big should this be?
        uint32_t read = 0;
        auto status = channel_.read(0, buf, sizeof(buf), &read, nullptr, 0, nullptr);
        if (status != NO_ERROR) {
            errorf("could not read channel: %d\n", status);
            channel_.reset();
            return;
        }
        debugf("read %u bytes from channel_\n", read);

        status = RegisterChannelWaitLocked();
        if (status != NO_ERROR) {
            errorf("could not wait on channel: %d\n", status);
            channel_.reset();
            return;
        }
    }
}

mx_status_t Device::RegisterChannelWaitLocked() {
    mx_signals_t sigs = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    return mx_object_wait_async(channel_.get(), port_.get(),
            reinterpret_cast<std::uintptr_t>(this), sigs, MX_WAIT_ASYNC_ONCE);
}

mx_status_t Device::SendShutdown() {
    debugfn();
    LoopMessage msg(kShutdown);
    return port_.queue(&msg, 0);
}

mx_status_t Device::GetChannel(mx::channel* out) {
    MX_DEBUG_ASSERT(out != nullptr);

    std::lock_guard<std::mutex> lock(lock_);
    if (!port_.is_valid()) {
        return ERR_BAD_STATE;
    }
    if (channel_.is_valid()) {
        return ERR_ALREADY_BOUND;
    }

    auto status = mx::channel::create(0, &channel_, out);
    if (status != NO_ERROR) {
        errorf("could not create channel: %d\n", status);
        return status;
    }

    status = RegisterChannelWaitLocked();
    if (status != NO_ERROR) {
        errorf("could not wait on channel: %d\n", status);
        out->reset();
        channel_.reset();
        return status;
    }

    infof("channel opened\n");
    return NO_ERROR;
}

}  // namespace wlan
